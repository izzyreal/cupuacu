#include "RevisionEffect.hpp"
#include "../../effects/AmplifyFadeEffect.hpp"
#include "../../effects/AmplifyEnvelopeEffect.hpp"
#include "../../effects/DynamicsEffect.hpp"
#include "../../waveform/DecodedWaveformBuilder.hpp"
#include "../../concurrency/DeferredRelease.hpp"
#include <array>
#include <set>

namespace cupuacu::actions::effects
{
    std::unique_ptr<BackgroundEffectResult> computeRevisionEffect(
        const BackgroundEffectRequest &request,
        std::shared_ptr<const storage::AudioEditRevision> source,
        const std::filesystem::path &directory,
        const std::function<void(const std::string &, std::optional<double>)>
            &progress)
    {
        const auto shape = source->shape();
        std::set<int64_t> uniqueChannels;
        if (request.frameCount <= 0 || request.targetChannels.empty())
        {
            throw std::invalid_argument(
                "Effect requires a nonempty audio range");
        }
        for (auto channel : request.targetChannels)
        {
            storage::AudioReader::validateRange(
                shape, int(channel), request.startFrame,
                std::size_t(request.frameCount));
            if (!uniqueChannels.insert(channel).second)
            {
                throw std::invalid_argument("Duplicate effect channel");
            }
        }
        auto result = std::make_unique<BackgroundEffectResult>();
        result->kind = request.kind;
        result->targetTabId = request.targetTabId;
        result->targetTabIndex = request.targetTabIndex;
        result->startFrame = request.startFrame;
        result->frameCount = request.frameCount;
        result->targetChannels = request.targetChannels;
        result->originalCursor = request.originalCursor;
        result->hadSelection = request.hadSelection;
        result->originalRelevantLength = request.frameCount;
        result->beforeRevision = concurrency::releaseOnWorker(source);
        storage::AudioEditTransaction edit(*source);
        constexpr int64_t blockFrames = 16384;
        std::array<float, blockFrames> buffer;
        const auto publish = [&](double fraction)
        {
            if (progress)
            {
                progress(request.description, fraction);
            }
        };
        publish(0);
        if (request.kind == BackgroundEffectKind::RemoveSilence)
        {
            if (!request.removeSilenceSettings)
            {
                throw std::invalid_argument("Missing remove silence settings");
            }
            const auto &settings = *request.removeSilenceSettings;
            const auto threshold =
                ::cupuacu::effects::removeSilenceThresholdUnitFromIndex(
                    settings.thresholdUnitIndex) ==
                        ::cupuacu::effects::RemoveSilenceThresholdUnit::Db
                    ? ::cupuacu::effects::thresholdAbsoluteFromDb(
                          settings.thresholdDb)
                    : ::cupuacu::effects::thresholdAbsoluteFromSampleValue(
                          shape.format, settings.thresholdSampleValue);
            const int64_t minimum = std::max<int64_t>(
                1, int64_t(std::ceil(std::clamp(settings.minimumSilenceLengthMs,
                                                0.0, 5000.0) *
                                     shape.sampleRate / 1000.0)));
            std::array<float, blockFrames> magnitudes;
            std::optional<int64_t> runStart;
            auto &runs = result->silenceRuns;
            const auto end = request.startFrame + request.frameCount;
            auto finishRun = [&](int64_t frame)
            {
                if (runStart && frame - *runStart >= minimum)
                {
                    runs.push_back({*runStart, frame - *runStart});
                }
                runStart.reset();
            };
            for (int64_t first = request.startFrame; first < end;)
            {
                const auto count = std::min(blockFrames, end - first);
                std::fill_n(magnitudes.begin(), count, 0.f);
                for (auto channel : request.targetChannels)
                {
                    publish(double(first - request.startFrame) /
                            request.frameCount * 0.9);
                    source->readChannel(int(channel), first,
                                        std::span(buffer).first(count));
                    for (int64_t i = 0; i < count; ++i)
                    {
                        magnitudes[i] =
                            std::max(magnitudes[i], std::fabs(buffer[i]));
                    }
                }
                for (int64_t i = 0; i < count; ++i)
                {
                    if (magnitudes[i] <= threshold)
                    {
                        if (!runStart)
                        {
                            runStart = first + i;
                        }
                    }
                    else
                    {
                        finishRun(first + i);
                    }
                }
                first += count;
            }
            finishRun(end);
            if (::cupuacu::effects::removeSilenceModeFromIndex(
                    settings.modeIndex) ==
                ::cupuacu::effects::RemoveSilenceMode::FromBeginningAndEnd)
            {
                std::erase_if(runs,
                              [&](const auto &run)
                              {
                                  return run.startFrame != request.startFrame &&
                                         run.startFrame + run.frameCount != end;
                              });
            }
            result->removeSilenceRemovesDuration =
                uniqueChannels.size() == std::size_t(shape.channels);
            if (result->removeSilenceRemovesDuration)
            {
                for (auto it = runs.rbegin(); it != runs.rend(); ++it)
                {
                    publish(0.95);
                    edit.erase(it->startFrame, it->frameCount);
                }
            }
            else
            {
                // Build the compacted range once, preserving original source
                // coordinates; map only selected channels and pad with silence.
                storage::AudioEditTransaction compact(*source);
                compact.trim(request.startFrame, request.frameCount);
                int64_t removed = 0;
                for (auto it = runs.rbegin(); it != runs.rend(); ++it)
                {
                    publish(0.95);
                    compact.erase(it->startFrame - request.startFrame,
                                  it->frameCount);
                    removed += it->frameCount;
                }
                auto kept = compact.finish();
                for (auto channel : request.targetChannels)
                {
                    edit.replaceChannel(int(channel), request.startFrame,
                                        kept->shape().frames, kept.get(),
                                        int(channel));
                    edit.replaceChannel(
                        int(channel), request.startFrame + kept->shape().frames,
                        removed);
                }
            }
        }
        else
        {
            // Share the generated-sample cache across effects and history.
            // Retaining another undo root must not create another cache budget.
            static const auto cache = storage::defaultDecodedBlockCache();
            auto store = std::make_shared<storage::AudioBlockStore>(directory);
            for (std::size_t index = 0; index < request.targetChannels.size();
                 ++index)
            {
                storage::AudioShape outputShape{request.frameCount, 1,
                                                shape.sampleRate, shape.format};
                waveform::DecodedWaveformBuilder peaks;
                storage::AudioRevisionBuilder builder(
                    outputShape, store, cache,
                    [&](int64_t first,
                        std::span<
                            const storage::AudioRevisionBuilder::PendingChannel>
                            channels,
                        uint32_t count)
                    {
                        peaks.appendFrom(outputShape, first + count,
                                         [&](int channel, int64_t start,
                                             std::span<float> output)
                                         {
                                             std::copy_n(
                                                 channels[channel].data() +
                                                     start - first,
                                                 output.size(), output.data());
                                         });
                    });
                for (int64_t first = 0; first < request.frameCount;)
                {
                    const auto count =
                        std::min(blockFrames, request.frameCount - first);
                    publish(
                        (double(index) + double(first) / request.frameCount) /
                        request.targetChannels.size());
                    const auto sourceStart =
                        request.kind == BackgroundEffectKind::Reverse
                            ? request.startFrame + request.frameCount - first -
                                  count
                            : request.startFrame + first;
                    auto samples = std::span(buffer).first(count);
                    source->readChannel(int(request.targetChannels[index]),
                                        sourceStart, samples);
                    if (request.kind == BackgroundEffectKind::Reverse)
                    {
                        std::reverse(samples.begin(), samples.end());
                    }
                    else
                    {
                        for (int64_t i = 0; i < count; ++i)
                        {
                            switch (request.kind)
                            {
                                case BackgroundEffectKind::AmplifyFade:
                                    if (!request.amplifyFadeSettings)
                                    {
                                        throw std::invalid_argument(
                                            "Missing gain settings");
                                    }
                                    buffer[i] = float(
                                        buffer[i] *
                                        ::cupuacu::effects::AmplifyFadeUndoable::
                                            gainForRelativeFrame(
                                                *request.amplifyFadeSettings,
                                                ::cupuacu::effects::
                                                    AmplifyFadeUndoable::
                                                        clampCurve(
                                                            request
                                                                .amplifyFadeSettings
                                                                ->curveIndex),
                                                first + i, request.frameCount));
                                    break;
                                case BackgroundEffectKind::Dynamics:
                                    if (!request.dynamicsSettings)
                                    {
                                        throw std::invalid_argument(
                                            "Missing dynamics settings");
                                    }
                                    buffer[i] = ::cupuacu::effects::
                                        DynamicsUndoable::processSampleValue(
                                            *request.dynamicsSettings,
                                            buffer[i]);
                                    break;
                                case BackgroundEffectKind::AmplifyEnvelope:
                                    if (!request.amplifyEnvelopeSettings)
                                    {
                                        throw std::invalid_argument(
                                            "Missing envelope settings");
                                    }
                                    buffer[i] = float(
                                        buffer[i] *
                                        ::cupuacu::effects::
                                            amplifyEnvelopeGainForRelativeFrame(
                                                *request
                                                     .amplifyEnvelopeSettings,
                                                first + i, request.frameCount));
                                    break;
                                default:
                                    throw std::invalid_argument(
                                        "Unsupported revision effect");
                            }
                        }
                    }
                    builder.appendInterleaved(samples);
                    first += count;
                }
                auto caches = peaks.takeCaches();
                std::vector<std::vector<gui::PeakLevel>> levels{
                    caches.getCache(0).snapshotBuildState().levels};
                auto generated =
                    storage::AudioEditRevision::from(builder.finish(
                        {}, waveform::SourcePeaks::createPaged(
                                outputShape, std::move(levels), cache,
                                [&]
                                {
                                    publish(double(index + 1) /
                                            request.targetChannels.size());
                                    return false;
                                })));
                edit.replaceChannel(int(request.targetChannels[index]),
                                    request.startFrame, request.frameCount,
                                    generated.get());
            }
        }
        publish(1); // Last cancellation check before publishing a candidate.
        result->afterRevision = concurrency::releaseOnWorker(edit.finish());
        return result;
    }
} // namespace cupuacu::actions::effects
