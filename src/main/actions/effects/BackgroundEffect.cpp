#include "BackgroundEffect.hpp"

#include "../MutationAvailability.hpp"
#include "../DocumentSessionPersistence.hpp"
#include "../../LongTask.hpp"
#include "../../effects/AmplifyFadeEffect.hpp"
#include "../../effects/AmplifyEnvelopeEffect.hpp"
#include "../../effects/DynamicsEffect.hpp"
#include "../../effects/EffectTargeting.hpp"
#include "../../effects/RemoveSilenceEffect.hpp"
#include "../../effects/ReverseEffect.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <exception>
#include <utility>

namespace cupuacu::actions::effects
{
    namespace
    {
        std::uint64_t nextBackgroundEffectJobId()
        {
            static std::uint64_t nextId = 1;
            return nextId++;
        }

        void reportEffectFailure(cupuacu::State *state,
                                 const std::string &description,
                                 const std::string &reason)
        {
            const std::string title = description.empty() ? "Effect failed"
                                                          : description + " failed";
            const std::string message =
                reason.empty() ? "An unknown error occurred." : reason;

            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", title.c_str(),
                         message.c_str());
            if (state && state->errorReporter)
            {
                state->errorReporter(title, message);
                return;
            }

            if (!state || SDL_WasInit(SDL_INIT_VIDEO) == 0)
            {
                return;
            }

            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR, title.c_str(), message.c_str(),
                state->mainDocumentSessionWindow &&
                        state->mainDocumentSessionWindow->getWindow()
                    ? state->mainDocumentSessionWindow->getWindow()->getSdlWindow()
                    : nullptr);
        }

        bool canStartEffect(cupuacu::State *state)
        {
            return state != nullptr && !state->backgroundOpenJob &&
                   !state->backgroundSaveJob && !state->backgroundEffectJob &&
                   !state->longTask.active &&
                   cupuacu::actions::isDocumentMutationAvailable(state);
        }

        int findTabIndexById(const cupuacu::State *state, const uint64_t tabId)
        {
            if (!state || tabId == 0)
            {
                return -1;
            }
            for (int index = 0; index < static_cast<int>(state->tabs.size());
                 ++index)
            {
                if (state->tabs[static_cast<std::size_t>(index)].id == tabId)
                {
                    return index;
                }
            }
            return -1;
        }

        class PreparedDocumentUndoable final
            : public cupuacu::actions::Undoable
        {
        public:
            PreparedDocumentUndoable(
                cupuacu::State *stateToUse, const int tabIndexToUse,
                std::shared_ptr<cupuacu::actions::Undoable> persistenceDelegate,
                cupuacu::Document preparedDocument,
                waveform::DocumentWaveformCaches preparedWaveformCaches,
                std::function<void(bool)> afterSwapToUse = {})
                : Undoable(stateToUse), tabIndex(tabIndexToUse),
                  delegate(std::move(persistenceDelegate)),
                  redoRevision(std::move(preparedDocument)),
                  redoWaveformCaches(std::move(preparedWaveformCaches)),
                  afterSwap(std::move(afterSwapToUse))
            {
                updateGui = [this]
                {
                    if (delegate)
                    {
                        delegate->updateGui();
                    }
                };
            }

            void redo() override
            {
                swapRevision(redoRevision, undoRevision,
                             redoWaveformCaches, undoWaveformCaches, true);
            }

            void undo() override
            {
                swapRevision(undoRevision, redoRevision,
                             undoWaveformCaches, redoWaveformCaches, false);
            }

            std::string getRedoDescription() override
            {
                return delegate ? delegate->getRedoDescription()
                                : std::string{"Effect"};
            }

            std::string getUndoDescription() override
            {
                return delegate ? delegate->getUndoDescription()
                                : std::string{"Effect"};
            }

            [[nodiscard]] cupuacu::file::OverwritePreservationMutation
            overwritePreservationMutation() const override
            {
                return delegate
                           ? delegate->overwritePreservationMutation()
                           : cupuacu::file::OverwritePreservationMutationHelper::
                                 incompatible("Prepared effect");
            }

            [[nodiscard]] bool canPersistForRestart() const override
            {
                return delegate && delegate->canPersistForRestart();
            }

            [[nodiscard]] std::optional<nlohmann::json>
            serializeForRestart() const override
            {
                return delegate ? delegate->serializeForRestart()
                                : std::nullopt;
            }

        private:
            int tabIndex = -1;
            std::shared_ptr<cupuacu::actions::Undoable> delegate;
            std::optional<cupuacu::Document> redoRevision;
            std::optional<cupuacu::Document> undoRevision;
            std::optional<cupuacu::waveform::DocumentWaveformCaches>
                redoWaveformCaches;
            std::optional<cupuacu::waveform::DocumentWaveformCaches>
                undoWaveformCaches;
            std::function<void(bool)> afterSwap;

            void swapRevision(std::optional<cupuacu::Document> &source,
                              std::optional<cupuacu::Document> &destination,
                              std::optional<
                                  cupuacu::waveform::DocumentWaveformCaches>
                                  &sourceCaches,
                              std::optional<
                                  cupuacu::waveform::DocumentWaveformCaches>
                                  &destinationCaches,
                              const bool isRedo)
            {
                if (!state || !source.has_value() ||
                    !sourceCaches.has_value() || tabIndex < 0 ||
                    tabIndex >= static_cast<int>(state->tabs.size()))
                {
                    return;
                }

                auto &session =
                    state->tabs[static_cast<std::size_t>(tabIndex)].session;
                session.stopWaveformCacheBuild();
                destination.emplace(std::move(session.document));
                destinationCaches.emplace(
                    std::move(session.waveformCaches));
                session.document = std::move(*source);
                session.waveformCaches = std::move(*sourceCaches);
                source.reset();
                sourceCaches.reset();
                session.updateWaveformCache();
                session.syncSelectionAndCursorToDocumentLength();
                if (afterSwap)
                {
                    afterSwap(isRedo);
                }
            }
        };

        void startBackgroundEffect(cupuacu::State *state,
                                   BackgroundEffectRequest request,
                                   const cupuacu::Document *document)
        {
            if (!state || !document || request.frameCount <= 0 ||
                request.targetChannels.empty() || state->backgroundEffectJob)
            {
                return;
            }

            cupuacu::actions::detail::ensureUndoStoreForTab(
                state, request.targetTabIndex);
            const auto &session =
                state->tabs[static_cast<std::size_t>(request.targetTabIndex)]
                    .session;
            state->backgroundEffectJob.reset(new BackgroundEffectJob(
                nextBackgroundEffectJobId(), std::move(request), *document,
                session.undoStore, &session.waveformCaches));
            cupuacu::setLongTask(state, "Applying effect",
                                 state->backgroundEffectJob->snapshot().detail,
                                 0.0, false, true);
            state->backgroundEffectJob->start();
        }

        std::unique_ptr<BackgroundEffectResult>
        computeReverseResult(const BackgroundEffectRequest &request,
                             const cupuacu::Document::ReadLease &document,
                             const std::function<void(const std::string &,
                                                      std::optional<double>)>
                                 &progress)
        {
            auto result = std::make_unique<BackgroundEffectResult>();
            result->kind = request.kind;
            result->targetTabIndex = request.targetTabIndex;
            result->targetTabId = request.targetTabId;
            result->startFrame = request.startFrame;
            result->frameCount = request.frameCount;
            result->targetChannels = request.targetChannels;

            result->oldSamples.resize(request.targetChannels.size());
            result->newSamples.resize(request.targetChannels.size());

            constexpr int64_t progressStrideFrames = 16384;
            if (progress)
            {
                progress(request.description, 0.0);
            }

            for (std::size_t channelIndex = 0;
                 channelIndex < request.targetChannels.size(); ++channelIndex)
            {
                const int64_t channel = request.targetChannels[channelIndex];
                auto &oldChannel = result->oldSamples[channelIndex];
                auto &newChannel = result->newSamples[channelIndex];
                oldChannel.resize(static_cast<std::size_t>(request.frameCount));
                newChannel.resize(static_cast<std::size_t>(request.frameCount));
                CUPUACU_METRIC(result->observedCapacity.set(
                    performance::matrixCapacity(result->oldSamples) +
                    performance::matrixCapacity(result->newSamples)));
                CUPUACU_METRIC(
                    performance::add(performance::Work::SampleBytesCopied,
                                     request.frameCount * sizeof(float)));
                CUPUACU_METRIC(performance::add(
                    performance::Work::SamplesScanned, request.frameCount));

                for (int64_t frame = 0; frame < request.frameCount; ++frame)
                {
                    oldChannel[static_cast<std::size_t>(frame)] =
                        document.getSample(channel, request.startFrame + frame);
                }

                for (int64_t frame = 0; frame < request.frameCount; ++frame)
                {
                    newChannel[static_cast<std::size_t>(frame)] =
                        oldChannel[static_cast<std::size_t>(
                            request.frameCount - 1 - frame)];
                    if (progress &&
                        ((frame + 1) % progressStrideFrames == 0 ||
                         frame + 1 == request.frameCount))
                    {
                        progress(
                            request.description,
                            static_cast<double>(frame + 1) /
                                static_cast<double>(request.frameCount));
                    }
                }
            }

            return result;
        }

        std::unique_ptr<BackgroundEffectResult>
        computeAmplifyFadeResult(
            const BackgroundEffectRequest &request,
            const cupuacu::Document::ReadLease &document,
            const std::function<void(const std::string &,
                                     std::optional<double>)> &progress)
        {
            if (!request.amplifyFadeSettings.has_value())
            {
                throw std::runtime_error(
                    "Background amplify/fade job is missing settings");
            }

            auto result = std::make_unique<BackgroundEffectResult>();
            result->kind = request.kind;
            result->targetTabIndex = request.targetTabIndex;
            result->targetTabId = request.targetTabId;
            result->startFrame = request.startFrame;
            result->frameCount = request.frameCount;
            result->targetChannels = request.targetChannels;

            result->oldSamples.resize(request.targetChannels.size());
            result->newSamples.resize(request.targetChannels.size());

            const auto curve =
                cupuacu::effects::AmplifyFadeUndoable::clampCurve(
                    request.amplifyFadeSettings->curveIndex);
            constexpr int64_t progressStrideFrames = 16384;
            if (progress)
            {
                progress(request.description, 0.0);
            }

            for (std::size_t channelIndex = 0;
                 channelIndex < request.targetChannels.size(); ++channelIndex)
            {
                const int64_t channel = request.targetChannels[channelIndex];
                auto &oldChannel = result->oldSamples[channelIndex];
                auto &newChannel = result->newSamples[channelIndex];
                oldChannel.resize(static_cast<std::size_t>(request.frameCount));
                newChannel.resize(static_cast<std::size_t>(request.frameCount));
                CUPUACU_METRIC(result->observedCapacity.set(
                    performance::matrixCapacity(result->oldSamples) +
                    performance::matrixCapacity(result->newSamples)));
                CUPUACU_METRIC(
                    performance::add(performance::Work::SampleBytesCopied,
                                     request.frameCount * sizeof(float)));
                CUPUACU_METRIC(performance::add(
                    performance::Work::SamplesScanned, request.frameCount));

                for (int64_t frame = 0; frame < request.frameCount; ++frame)
                {
                    const float oldValue =
                        document.getSample(channel, request.startFrame + frame);
                    oldChannel[static_cast<std::size_t>(frame)] = oldValue;
                    newChannel[static_cast<std::size_t>(frame)] =
                        static_cast<float>(
                            oldValue *
                            cupuacu::effects::AmplifyFadeUndoable::
                                gainForRelativeFrame(
                                    *request.amplifyFadeSettings, curve, frame,
                                    request.frameCount));
                    if (progress &&
                        ((frame + 1) % progressStrideFrames == 0 ||
                         frame + 1 == request.frameCount))
                    {
                        progress(
                            request.description,
                            static_cast<double>(frame + 1) /
                                static_cast<double>(request.frameCount));
                    }
                }
            }

            return result;
        }

        std::unique_ptr<BackgroundEffectResult>
        computeDynamicsResult(
            const BackgroundEffectRequest &request,
            const cupuacu::Document::ReadLease &document,
            const std::function<void(const std::string &,
                                     std::optional<double>)> &progress)
        {
            if (!request.dynamicsSettings.has_value())
            {
                throw std::runtime_error(
                    "Background dynamics job is missing settings");
            }

            auto result = std::make_unique<BackgroundEffectResult>();
            result->kind = request.kind;
            result->targetTabIndex = request.targetTabIndex;
            result->targetTabId = request.targetTabId;
            result->startFrame = request.startFrame;
            result->frameCount = request.frameCount;
            result->targetChannels = request.targetChannels;

            result->oldSamples.resize(request.targetChannels.size());
            result->newSamples.resize(request.targetChannels.size());

            constexpr int64_t progressStrideFrames = 16384;
            if (progress)
            {
                progress(request.description, 0.0);
            }

            for (std::size_t channelIndex = 0;
                 channelIndex < request.targetChannels.size(); ++channelIndex)
            {
                const int64_t channel = request.targetChannels[channelIndex];
                auto &oldChannel = result->oldSamples[channelIndex];
                auto &newChannel = result->newSamples[channelIndex];
                oldChannel.resize(static_cast<std::size_t>(request.frameCount));
                newChannel.resize(static_cast<std::size_t>(request.frameCount));
                CUPUACU_METRIC(result->observedCapacity.set(
                    performance::matrixCapacity(result->oldSamples) +
                    performance::matrixCapacity(result->newSamples)));
                CUPUACU_METRIC(
                    performance::add(performance::Work::SampleBytesCopied,
                                     request.frameCount * sizeof(float)));
                CUPUACU_METRIC(performance::add(
                    performance::Work::SamplesScanned, request.frameCount));

                for (int64_t frame = 0; frame < request.frameCount; ++frame)
                {
                    const float oldValue =
                        document.getSample(channel, request.startFrame + frame);
                    oldChannel[static_cast<std::size_t>(frame)] = oldValue;
                    newChannel[static_cast<std::size_t>(frame)] =
                        cupuacu::effects::DynamicsUndoable::processSampleValue(
                            *request.dynamicsSettings, oldValue);
                    if (progress &&
                        ((frame + 1) % progressStrideFrames == 0 ||
                         frame + 1 == request.frameCount))
                    {
                        progress(
                            request.description,
                            static_cast<double>(frame + 1) /
                                static_cast<double>(request.frameCount));
                    }
                }
            }

            return result;
        }

        std::unique_ptr<BackgroundEffectResult>
        computeAmplifyEnvelopeResult(
            const BackgroundEffectRequest &request,
            const cupuacu::Document::ReadLease &document,
            const std::function<void(const std::string &,
                                     std::optional<double>)> &progress)
        {
            if (!request.amplifyEnvelopeSettings.has_value())
            {
                throw std::runtime_error(
                    "Background amplify envelope job is missing settings");
            }

            auto result = std::make_unique<BackgroundEffectResult>();
            result->kind = request.kind;
            result->targetTabIndex = request.targetTabIndex;
            result->targetTabId = request.targetTabId;
            result->startFrame = request.startFrame;
            result->frameCount = request.frameCount;
            result->targetChannels = request.targetChannels;

            result->oldSamples.resize(request.targetChannels.size());
            result->newSamples.resize(request.targetChannels.size());

            constexpr int64_t progressStrideFrames = 16384;
            if (progress)
            {
                progress(request.description, 0.0);
            }

            for (std::size_t channelIndex = 0;
                 channelIndex < request.targetChannels.size(); ++channelIndex)
            {
                const int64_t channel = request.targetChannels[channelIndex];
                auto &oldChannel = result->oldSamples[channelIndex];
                auto &newChannel = result->newSamples[channelIndex];
                oldChannel.resize(static_cast<std::size_t>(request.frameCount));
                newChannel.resize(static_cast<std::size_t>(request.frameCount));
                CUPUACU_METRIC(result->observedCapacity.set(
                    performance::matrixCapacity(result->oldSamples) +
                    performance::matrixCapacity(result->newSamples)));
                CUPUACU_METRIC(
                    performance::add(performance::Work::SampleBytesCopied,
                                     request.frameCount * sizeof(float)));
                CUPUACU_METRIC(performance::add(
                    performance::Work::SamplesScanned, request.frameCount));

                for (int64_t frame = 0; frame < request.frameCount; ++frame)
                {
                    const float oldValue =
                        document.getSample(channel, request.startFrame + frame);
                    oldChannel[static_cast<std::size_t>(frame)] = oldValue;
                    newChannel[static_cast<std::size_t>(frame)] =
                        static_cast<float>(
                            oldValue *
                            cupuacu::effects::amplifyEnvelopeGainForRelativeFrame(
                                *request.amplifyEnvelopeSettings, frame,
                                request.frameCount));
                    if (progress &&
                        ((frame + 1) % progressStrideFrames == 0 ||
                         frame + 1 == request.frameCount))
                    {
                        progress(
                            request.description,
                            static_cast<double>(frame + 1) /
                                static_cast<double>(request.frameCount));
                    }
                }
            }

            return result;
        }

        std::unique_ptr<BackgroundEffectResult>
        computeRemoveSilenceResult(
            const BackgroundEffectRequest &request,
            const cupuacu::Document::ReadLease &document,
            const std::function<void(const std::string &,
                                     std::optional<double>)> &progress)
        {
            if (!request.removeSilenceSettings.has_value())
            {
                throw std::runtime_error(
                    "Background remove silence job is missing settings");
            }

            const auto &settings = *request.removeSilenceSettings;
            const double thresholdAbsolute =
                cupuacu::effects::removeSilenceThresholdUnitFromIndex(
                    settings.thresholdUnitIndex) ==
                        cupuacu::effects::RemoveSilenceThresholdUnit::Db
                    ? cupuacu::effects::thresholdAbsoluteFromDb(
                          settings.thresholdDb)
                    : cupuacu::effects::thresholdAbsoluteFromSampleValue(
                          document.getSampleFormat(),
                          settings.thresholdSampleValue);
            const auto mode =
                cupuacu::effects::removeSilenceModeFromIndex(settings.modeIndex);
            std::vector<cupuacu::effects::SilenceRange> runs;
            const int64_t endFrame = request.startFrame + request.frameCount;
            const int64_t minFrames = [&]
            {
                const int sampleRate = document.getSampleRate();
                if (sampleRate <= 0)
                {
                    return int64_t{1};
                }

                const double clampedMs =
                    std::clamp(settings.minimumSilenceLengthMs, 0.0, 5000.0);
                const double frames =
                    clampedMs * static_cast<double>(sampleRate) / 1000.0;
                return std::max<int64_t>(
                    1, static_cast<int64_t>(std::ceil(frames)));
            }();
            bool inRun = false;
            int64_t runStart = request.startFrame;
            for (int64_t frame = request.startFrame; frame < endFrame; ++frame)
            {
                double magnitude = 0.0;
                for (const int64_t channel : request.targetChannels)
                {
                    magnitude = std::max(
                        magnitude,
                        static_cast<double>(std::fabs(
                            document.getSample(channel, frame))));
                }
                const bool isSilent = magnitude <= thresholdAbsolute;
                if (isSilent && !inRun)
                {
                    inRun = true;
                    runStart = frame;
                }
                else if (!isSilent && inRun)
                {
                    const int64_t runLength = frame - runStart;
                    if (runLength >= minFrames)
                    {
                        runs.push_back(
                            {.startFrame = runStart, .frameCount = runLength});
                    }
                    inRun = false;
                }
            }

            if (inRun)
            {
                const int64_t runLength = endFrame - runStart;
                if (runLength >= minFrames)
                {
                    runs.push_back(
                        {.startFrame = runStart, .frameCount = runLength});
                }
            }

            if (mode == cupuacu::effects::RemoveSilenceMode::FromBeginningAndEnd &&
                !runs.empty())
            {
                std::vector<cupuacu::effects::SilenceRange> trimmedRuns;
                if (runs.front().startFrame == request.startFrame)
                {
                    trimmedRuns.push_back(runs.front());
                }
                if (runs.size() > 1 &&
                    runs.back().startFrame + runs.back().frameCount == endFrame)
                {
                    trimmedRuns.push_back(runs.back());
                }
                else if (runs.size() == 1 &&
                         runs.front().startFrame + runs.front().frameCount ==
                             endFrame &&
                         trimmedRuns.empty())
                {
                    trimmedRuns.push_back(runs.front());
                }
                runs = std::move(trimmedRuns);
            }

            auto result = std::make_unique<BackgroundEffectResult>();
            result->kind = request.kind;
            result->targetTabIndex = request.targetTabIndex;
            result->targetTabId = request.targetTabId;
            result->startFrame = request.startFrame;
            result->frameCount = request.frameCount;
            result->targetChannels = request.targetChannels;
            result->silenceRuns = runs;
            result->originalRelevantLength = request.frameCount;
            result->originalCursor = request.originalCursor;
            result->hadSelection = request.hadSelection;

            if (progress)
            {
                progress(request.description, runs.empty() ? 1.0 : 0.0);
            }

            if (runs.empty())
            {
                return result;
            }

            const bool removesDuration =
                static_cast<int64_t>(request.targetChannels.size()) ==
                document.getChannelCount();
            result->removeSilenceRemovesDuration = removesDuration;

            if (removesDuration)
            {
                result->removedSamples.resize(runs.size());

                const int64_t channelCount = document.getChannelCount();
                for (std::size_t runIndex = 0; runIndex < runs.size(); ++runIndex)
                {
                    const auto &run = runs[runIndex];
                    result->removedSamples[runIndex].resize(
                        static_cast<std::size_t>(channelCount));
                    for (int64_t channel = 0; channel < channelCount; ++channel)
                    {
                        auto &channelSamples =
                            result->removedSamples[runIndex][channel];
                        channelSamples.resize(
                            static_cast<std::size_t>(run.frameCount));
                        for (int64_t frame = 0; frame < run.frameCount; ++frame)
                        {
                            channelSamples[static_cast<std::size_t>(frame)] =
                                document.getSample(channel,
                                                   run.startFrame + frame);
                        }
                    }
                }
                if (progress)
                {
                    progress(request.description, 1.0);
                }
                return result;
            }

            result->oldSamples.resize(request.targetChannels.size());
            result->newSamples.resize(request.targetChannels.size());

            for (std::size_t channelIndex = 0;
                 channelIndex < request.targetChannels.size(); ++channelIndex)
            {
                const int64_t channel = request.targetChannels[channelIndex];
                auto &oldChannel = result->oldSamples[channelIndex];
                auto &newChannel = result->newSamples[channelIndex];
                oldChannel.resize(static_cast<std::size_t>(request.frameCount));
                newChannel.assign(static_cast<std::size_t>(request.frameCount), 0.0f);

                for (int64_t frame = 0; frame < request.frameCount; ++frame)
                {
                    oldChannel[static_cast<std::size_t>(frame)] =
                        document.getSample(channel, request.startFrame + frame);
                }

                int64_t writeFrame = 0;
                std::size_t runIndex = 0;
                for (int64_t frame = 0; frame < request.frameCount; ++frame)
                {
                    const int64_t absoluteFrame = request.startFrame + frame;
                    while (runIndex < runs.size() &&
                           absoluteFrame >= runs[runIndex].startFrame +
                                                runs[runIndex].frameCount)
                    {
                        ++runIndex;
                    }

                    const bool insideRun =
                        runIndex < runs.size() &&
                        absoluteFrame >= runs[runIndex].startFrame &&
                        absoluteFrame <
                            runs[runIndex].startFrame + runs[runIndex].frameCount;
                    if (insideRun)
                    {
                        continue;
                    }

                    if (writeFrame < request.frameCount)
                    {
                        newChannel[static_cast<std::size_t>(writeFrame)] =
                            oldChannel[static_cast<std::size_t>(frame)];
                        ++writeFrame;
                    }
                }
            }

            if (progress)
            {
                progress(request.description, 1.0);
            }
            return result;
        }

        void commitCompletedBackgroundEffect(cupuacu::State *state,
                                             BackgroundEffectJob &job)
        {
            const auto snapshot = job.snapshot();
            cupuacu::clearLongTask(state, false);

            if (snapshot.canceled)
            {
                return;
            }
            if (!snapshot.success)
            {
                reportEffectFailure(state, snapshot.request.description,
                                    snapshot.error);
                return;
            }

            auto result = job.takeResult();
            if (!result)
            {
                reportEffectFailure(state, snapshot.request.description,
                                    "The background effect job did not produce a result.");
                return;
            }

            const int targetTabIndex =
                findTabIndexById(state, result->targetTabId);
            if (targetTabIndex < 0)
            {
                reportEffectFailure(state, snapshot.request.description,
                                    "The target tab is no longer available.");
                return;
            }

            const auto addPrepared =
                [&](std::shared_ptr<cupuacu::actions::Undoable> delegate,
                    std::function<void(bool)> afterSwap = {})
            {
                if (!result->preparedDocument.has_value())
                {
                    reportEffectFailure(
                        state, snapshot.request.description,
                        "The effect did not produce a prepared document revision.");
                    return;
                }
                state->addAndDoUndoableToTab(
                    targetTabIndex,
                    std::make_shared<PreparedDocumentUndoable>(
                        state, targetTabIndex, std::move(delegate),
                        std::move(*result->preparedDocument),
                        std::move(result->preparedWaveformCaches),
                        std::move(afterSwap)));
            };

            switch (result->kind)
            {
                case BackgroundEffectKind::Reverse:
                    addPrepared(
                        std::make_shared<cupuacu::effects::ReverseUndoable>(
                            state, targetTabIndex, result->startFrame,
                            result->frameCount,
                            std::move(result->targetChannels),
                            std::move(result->oldSamplesHandle),
                            std::move(result->newSamplesHandle)));
                    break;
                case BackgroundEffectKind::AmplifyFade:
                    addPrepared(
                        std::make_shared<cupuacu::effects::AmplifyFadeUndoable>(
                            state, targetTabIndex,
                            snapshot.request.amplifyFadeSettings.value_or(
                                ::cupuacu::effects::AmplifyFadeSettings{}),
                            result->startFrame, result->frameCount,
                            std::move(result->targetChannels),
                            std::move(result->oldSamplesHandle),
                            std::move(result->newSamplesHandle)));
                    break;
                case BackgroundEffectKind::Dynamics:
                    addPrepared(
                        std::make_shared<cupuacu::effects::DynamicsUndoable>(
                            state, targetTabIndex,
                            snapshot.request.dynamicsSettings.value_or(
                                ::cupuacu::effects::DynamicsSettings{}),
                            result->startFrame, result->frameCount,
                            std::move(result->targetChannels),
                            std::move(result->oldSamplesHandle),
                            std::move(result->newSamplesHandle)));
                    break;
                case BackgroundEffectKind::AmplifyEnvelope:
                    addPrepared(
                        std::make_shared<
                            cupuacu::effects::AmplifyEnvelopeUndoable>(
                            state, targetTabIndex,
                            snapshot.request.amplifyEnvelopeSettings.value_or(
                                ::cupuacu::effects::
                                    AmplifyEnvelopeSettings{}),
                            result->startFrame, result->frameCount,
                            std::move(result->targetChannels),
                            std::move(result->oldSamplesHandle),
                            std::move(result->newSamplesHandle)));
                    break;
                case BackgroundEffectKind::RemoveSilence:
                    if (result->silenceRuns.empty())
                    {
                        break;
                    }
                    if (result->removeSilenceRemovesDuration)
                    {
                        const auto relevantStart = result->startFrame;
                        const auto originalLength =
                            result->originalRelevantLength;
                        const auto originalCursor = result->originalCursor;
                        const auto hadSelection = result->hadSelection;
                        int64_t removedFrameCount = 0;
                        for (const auto &run : result->silenceRuns)
                        {
                            removedFrameCount += run.frameCount;
                        }
                        addPrepared(
                            std::make_shared<cupuacu::effects::RemoveSilenceUndoable>(
                                state, targetTabIndex,
                                std::move(result->silenceRuns),
                                relevantStart, originalLength,
                                std::move(result->removedSamplesHandle),
                                originalCursor, hadSelection),
                            [state, targetTabIndex, relevantStart,
                             originalLength, originalCursor, hadSelection,
                             removedFrameCount](const bool isRedo)
                            {
                                if (!state || targetTabIndex < 0 ||
                                    targetTabIndex >=
                                        static_cast<int>(state->tabs.size()))
                                {
                                    return;
                                }
                                auto &session =
                                    state->tabs[static_cast<std::size_t>(
                                                    targetTabIndex)]
                                        .session;
                                if (isRedo)
                                {
                                    session.cursor = relevantStart;
                                    if (hadSelection)
                                    {
                                        session.selection.setValue1(
                                            relevantStart);
                                        session.selection.setValue2(
                                            relevantStart +
                                            std::max<int64_t>(
                                                0, originalLength -
                                                       removedFrameCount));
                                    }
                                    else
                                    {
                                        session.selection.reset();
                                    }
                                }
                                else
                                {
                                    session.cursor = originalCursor;
                                    if (hadSelection)
                                    {
                                        session.selection.setValue1(
                                            relevantStart);
                                        session.selection.setValue2(
                                            relevantStart + originalLength);
                                    }
                                    else
                                    {
                                        session.selection.reset();
                                    }
                                }
                            });
                        break;
                    }

                    addPrepared(
                        std::make_shared<
                            cupuacu::effects::RemoveSilenceChannelCompactUndoable>(
                            state, targetTabIndex,
                            std::move(result->targetChannels),
                            std::move(result->silenceRuns), result->startFrame,
                            result->frameCount,
                            std::move(result->oldSamplesHandle),
                            std::move(result->newSamplesHandle)));
                    break;
            }
        }
    } // namespace

    BackgroundEffectJob::BackgroundEffectJob(
        std::uint64_t idToUse, BackgroundEffectRequest requestToRun,
        const cupuacu::Document &documentToRead, undo::UndoStore undoStoreToUse,
        const waveform::DocumentWaveformCaches *sourceCaches)
        : id(idToUse), request(std::move(requestToRun)),
          document(documentToRead), undoStore(std::move(undoStoreToUse)),
          detail(request.description)
    {
        // Freeze peaks with the same source revision as the audio. A running
        // cache worker is not copied; its unapplied ranges remain dirty.
        // Whole-document effects cannot reuse any peaks.
        const bool replacesAllSamples =
            request.startFrame == 0 &&
            request.frameCount == document.getFrameCount() &&
            request.targetChannels.size() ==
                static_cast<std::size_t>(document.getChannelCount());
        const bool mayRemoveDuration =
            request.kind == BackgroundEffectKind::RemoveSilence &&
            request.targetChannels.size() ==
                static_cast<std::size_t>(document.getChannelCount());
        if (sourceCaches && !replacesAllSamples && !mayRemoveDuration)
        {
            waveformCaches = sourceCaches->snapshotForDocument(document);
        }
        else
        {
            waveformCaches.resetToChannelCount(document.getChannelCount());
        }
    }

    BackgroundEffectJob::~BackgroundEffectJob()
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }

    void BackgroundEffectJob::start()
    {
        worker = std::thread([this]
                             { run(); });
    }

    BackgroundEffectJob::Snapshot BackgroundEffectJob::snapshot() const
    {
        std::lock_guard lock(mutex);
        return {
            .completed = completed,
            .success = success,
            .canceled = cancelRequested.load() && completed && !success,
            .request = request,
            .detail = detail,
            .progress = progress,
            .error = error,
        };
    }

    bool BackgroundEffectJob::waitForCompletion(
        const std::chrono::milliseconds timeout) const
    {
        std::unique_lock lock(mutex);
        return completionCv.wait_for(lock, timeout,
                                     [this]
                                     { return completed; });
    }

    std::unique_ptr<BackgroundEffectResult> BackgroundEffectJob::takeResult()
    {
        std::lock_guard lock(mutex);
        document = cupuacu::Document{};
        return std::move(result);
    }

    std::uint64_t BackgroundEffectJob::getId() const
    {
        return id;
    }

    void BackgroundEffectJob::cancel()
    {
        cancelRequested.store(true);
    }

    void BackgroundEffectJob::publishProgress(
        const std::string &detailToUse, std::optional<double> progressToUse)
    {
        std::lock_guard lock(mutex);
        detail = detailToUse;
        progress = progressToUse;
    }

    void BackgroundEffectJob::run()
    {
        try
        {
            const auto progressCallback =
                [this](const std::string &detailToUse,
                       std::optional<double> progressToUse)
            {
                if (cancelRequested.load())
                {
                    throw cupuacu::LongTaskCanceledError{};
                }
                publishProgress(detailToUse, progressToUse);
            };

            const auto lease = document.acquireReadLease();
            std::unique_ptr<BackgroundEffectResult> computedResult;
            switch (request.kind)
            {
                case BackgroundEffectKind::Reverse:
                    computedResult = computeReverseResult(request, lease,
                                                          progressCallback);
                    break;
                case BackgroundEffectKind::AmplifyFade:
                    computedResult = computeAmplifyFadeResult(
                        request, lease, progressCallback);
                    break;
                case BackgroundEffectKind::Dynamics:
                    computedResult = computeDynamicsResult(request, lease,
                                                           progressCallback);
                    break;
                case BackgroundEffectKind::AmplifyEnvelope:
                    computedResult = computeAmplifyEnvelopeResult(
                        request, lease, progressCallback);
                    break;
                case BackgroundEffectKind::RemoveSilence:
                    computedResult = computeRemoveSilenceResult(
                        request, lease, progressCallback);
                    break;
            }

            if (!computedResult)
            {
                throw std::runtime_error(
                    "Background effect did not produce a result");
            }

            if (!computedResult->silenceRuns.empty() &&
                computedResult->removeSilenceRemovesDuration)
            {
                cupuacu::Document prepared = document;
                for (auto it = computedResult->silenceRuns.rbegin();
                     it != computedResult->silenceRuns.rend(); ++it)
                {
                    prepared.removeFrames(it->startFrame, it->frameCount);
                }
                computedResult->preparedDocument = std::move(prepared);
                computedResult->preparedWaveformCaches.resetToChannelCount(
                    document.getChannelCount());
                if (undoStore.isAttached())
                {
                    computedResult->removedSamplesHandle =
                        undoStore.writeSampleCube(
                            computedResult->removedSamples,
                            "remove-silence-removed");
                }
            }
            else if (!computedResult->newSamples.empty())
            {
                cupuacu::Document prepared = document;
                for (std::size_t index = 0;
                     index < computedResult->targetChannels.size() &&
                     index < computedResult->newSamples.size();
                     ++index)
                {
                    const auto &samples = computedResult->newSamples[index];
                    prepared.writeChannelFloatBlock(
                        computedResult->targetChannels[index],
                        computedResult->startFrame, samples.data(),
                        static_cast<int64_t>(samples.size()), true);
                    if (!samples.empty())
                    {
                        waveformCaches
                            .getCache(static_cast<int>(
                                computedResult->targetChannels[index]))
                            .invalidateSamples(
                                computedResult->startFrame,
                                computedResult->startFrame +
                                    static_cast<int64_t>(samples.size()) - 1);
                    }
                }
                computedResult->preparedDocument = std::move(prepared);
                computedResult->preparedWaveformCaches =
                    std::move(waveformCaches);
                if (undoStore.isAttached())
                {
                    computedResult->oldSamplesHandle =
                        undoStore.writeSampleMatrix(computedResult->oldSamples,
                                                    "effect-old");
                    computedResult->newSamplesHandle =
                        undoStore.writeSampleMatrix(computedResult->newSamples,
                                                    "effect-new");
                }
            }

            std::lock_guard lock(mutex);
            result = std::move(computedResult);
            success = true;
            completed = true;
            completionCv.notify_all();
        }
        catch (const cupuacu::LongTaskCanceledError &e)
        {
            std::lock_guard lock(mutex);
            error = e.what();
            success = false;
            completed = true;
            completionCv.notify_all();
        }
        catch (const std::exception &e)
        {
            std::lock_guard lock(mutex);
            error = e.what();
            success = false;
            completed = true;
            completionCv.notify_all();
        }
        catch (...)
        {
            std::lock_guard lock(mutex);
            error = "An unknown error occurred.";
            success = false;
            completed = true;
            completionCv.notify_all();
        }
    }

    bool queueReverse(cupuacu::State *state)
    {
        if (!canStartEffect(state))
        {
            return false;
        }

        auto &session = state->getActiveDocumentSession();
        if (session.document.getFrameCount() <= 0 ||
            session.document.getChannelCount() <= 0)
        {
            return false;
        }

        if (session.selection.isActive() && session.selection.getLengthInt() <= 0)
        {
            return false;
        }

        int64_t startFrame = 0;
        int64_t frameCount = 0;
        if (!cupuacu::effects::getTargetRange(state, startFrame, frameCount))
        {
            return false;
        }

        const auto targetChannels = cupuacu::effects::getTargetChannels(state);
        if (targetChannels.empty())
        {
            return false;
        }

        startBackgroundEffect(
            state,
            BackgroundEffectRequest{
                .kind = BackgroundEffectKind::Reverse,
                .targetTabIndex = state->activeTabIndex,
                .targetTabId = state->getActiveTab()->id,
                .description = "Reverse",
                .startFrame = startFrame,
                .frameCount = frameCount,
                .targetChannels = targetChannels,
            },
            &session.document);
        return true;
    }

    bool queueAmplifyFade(
        cupuacu::State *state,
        const ::cupuacu::effects::AmplifyFadeSettings &settings)
    {
        if (!canStartEffect(state))
        {
            return false;
        }

        auto &session = state->getActiveDocumentSession();
        if (session.document.getFrameCount() <= 0 ||
            session.document.getChannelCount() <= 0)
        {
            return false;
        }

        if (session.selection.isActive() && session.selection.getLengthInt() <= 0)
        {
            return false;
        }

        int64_t startFrame = 0;
        int64_t frameCount = 0;
        if (!cupuacu::effects::getTargetRange(state, startFrame, frameCount))
        {
            return false;
        }

        const auto targetChannels = cupuacu::effects::getTargetChannels(state);
        if (targetChannels.empty())
        {
            return false;
        }

        startBackgroundEffect(
            state,
            BackgroundEffectRequest{
                .kind = BackgroundEffectKind::AmplifyFade,
                .targetTabIndex = state->activeTabIndex,
                .targetTabId = state->getActiveTab()->id,
                .description = "Amplify/Fade",
                .startFrame = startFrame,
                .frameCount = frameCount,
                .targetChannels = targetChannels,
                .amplifyFadeSettings = settings,
            },
            &session.document);
        return true;
    }

    bool queueDynamics(cupuacu::State *state,
                       const ::cupuacu::effects::DynamicsSettings &settings)
    {
        if (!canStartEffect(state))
        {
            return false;
        }

        auto &session = state->getActiveDocumentSession();
        if (session.document.getFrameCount() <= 0 ||
            session.document.getChannelCount() <= 0)
        {
            return false;
        }

        if (session.selection.isActive() && session.selection.getLengthInt() <= 0)
        {
            return false;
        }

        int64_t startFrame = 0;
        int64_t frameCount = 0;
        if (!cupuacu::effects::getTargetRange(state, startFrame, frameCount))
        {
            return false;
        }

        const auto targetChannels = cupuacu::effects::getTargetChannels(state);
        if (targetChannels.empty())
        {
            return false;
        }

        startBackgroundEffect(
            state,
            BackgroundEffectRequest{
                .kind = BackgroundEffectKind::Dynamics,
                .targetTabIndex = state->activeTabIndex,
                .targetTabId = state->getActiveTab()->id,
                .description = "Dynamics",
                .startFrame = startFrame,
                .frameCount = frameCount,
                .targetChannels = targetChannels,
                .dynamicsSettings = settings,
            },
            &session.document);
        return true;
    }

    bool queueAmplifyEnvelope(
        cupuacu::State *state,
        ::cupuacu::effects::AmplifyEnvelopeSettings settings)
    {
        if (!canStartEffect(state))
        {
            return false;
        }

        auto &session = state->getActiveDocumentSession();
        if (session.document.getFrameCount() <= 0 ||
            session.document.getChannelCount() <= 0)
        {
            return false;
        }

        if (session.selection.isActive() && session.selection.getLengthInt() <= 0)
        {
            return false;
        }

        cupuacu::effects::sanitizeAmplifyEnvelopeSettings(settings);

        int64_t startFrame = 0;
        int64_t frameCount = 0;
        if (!cupuacu::effects::getTargetRange(state, startFrame, frameCount))
        {
            return false;
        }

        const auto targetChannels = cupuacu::effects::getTargetChannels(state);
        if (targetChannels.empty())
        {
            return false;
        }

        startBackgroundEffect(
            state,
            BackgroundEffectRequest{
                .kind = BackgroundEffectKind::AmplifyEnvelope,
                .targetTabIndex = state->activeTabIndex,
                .targetTabId = state->getActiveTab()->id,
                .description = "Amplify Envelope",
                .startFrame = startFrame,
                .frameCount = frameCount,
                .targetChannels = targetChannels,
                .amplifyEnvelopeSettings = std::move(settings),
            },
            &session.document);
        return true;
    }

    bool queueRemoveSilence(
        cupuacu::State *state,
        const ::cupuacu::effects::RemoveSilenceSettings &settings)
    {
        if (!canStartEffect(state))
        {
            return false;
        }

        auto &session = state->getActiveDocumentSession();
        int64_t startFrame = 0;
        int64_t frameCount = 0;
        if (!cupuacu::effects::getTargetRange(state, startFrame, frameCount))
        {
            return false;
        }

        const auto targetChannels = cupuacu::effects::getTargetChannels(state);
        if (targetChannels.empty())
        {
            return false;
        }

        BackgroundEffectRequest request{
            .kind = BackgroundEffectKind::RemoveSilence,
            .targetTabIndex = state->activeTabIndex,
            .targetTabId = state->getActiveTab()->id,
            .description = "Remove silence",
            .startFrame = startFrame,
            .frameCount = frameCount,
            .targetChannels = targetChannels,
            .originalCursor = session.cursor,
            .hadSelection = session.selection.isActive(),
            .removeSilenceSettings = settings,
        };

        startBackgroundEffect(state, std::move(request), &session.document);
        return true;
    }

    void processPendingEffectWork(cupuacu::State *state)
    {
        if (!state || !state->backgroundEffectJob)
        {
            return;
        }

        if (cupuacu::isLongTaskCancelRequested(state))
        {
            state->backgroundEffectJob->cancel();
        }

        const auto snapshot = state->backgroundEffectJob->snapshot();
        if (snapshot.completed)
        {
            auto job = std::move(state->backgroundEffectJob);
            commitCompletedBackgroundEffect(state, *job);
            return;
        }

        cupuacu::updateLongTask(state, snapshot.detail, snapshot.progress,
                                false);
    }
} // namespace cupuacu::actions::effects
