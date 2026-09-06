#include "ClipboardConversion.hpp"
#include "AudioEditRevision.hpp"
#include "../waveform/DecodedWaveformBuilder.hpp"
#include "../LongTask.hpp"

namespace cupuacu::storage
{
    ClipboardAudio convertClipboard(const ClipboardAudio &clip, bool toRevision,
                                    const std::filesystem::path &path,
                                    const std::function<bool()> &cancel)
    {
        auto check = [&]
        {
            if (cancel && cancel())
            {
                throw LongTaskCanceledError{};
            }
        };
        check();
        if (bool(clip.getAudioRevision()) == toRevision)
        {
            return clip;
        }
        AudioShape shape{clip.getFrameCount(), int(clip.getChannelCount()),
                         clip.getSampleRate(), clip.getSampleFormat()};
        if (shape.frames <= 0 || shape.channels <= 0)
        {
            throw std::invalid_argument("Clipboard is empty");
        }
        ClipboardAudio result;
        constexpr int64_t chunk = 16384;
        if (toRevision)
        {
            auto store = std::make_shared<AudioBlockStore>(path);
            static const auto cache =
                std::make_shared<DecodedBlockCache>(4 * AudioBlockBytes);
            waveform::DecodedWaveformBuilder peaks;
            AudioRevisionBuilder builder(
                shape, store, cache,
                [&](int64_t first,
                    std::span<const AudioRevisionBuilder::PendingChannel>
                        channels,
                    uint32_t count)
                {
                    check();
                    peaks.appendFrom(
                        shape, first + count,
                        [&](int c, int64_t start, std::span<float> out)
                        {
                            std::copy_n(channels[c].data() + start - first,
                                        out.size(), out.data());
                        });
                });
            auto lease = clip.acquireReadLease();
            std::vector<float> interleaved(chunk * shape.channels);
            std::array<audio::SampleProvenance, chunk> provenance;
            std::array<uint8_t, chunk> dirty;
            for (int64_t first = 0; first < shape.frames; first += chunk)
            {
                check();
                const auto count = std::min(chunk, shape.frames - first);
                for (int c = 0; c < shape.channels; ++c)
                {
                    for (int64_t i = 0; i < count; ++i)
                    {
                        interleaved[i * shape.channels + c] =
                            lease.getSample(c, first + i);
                        provenance[i] = lease.getSampleProvenance(c, first + i);
                        dirty[i] = lease.isDirty(c, first + i);
                    }
                    builder.appendChannelMetadata(
                        c, first, std::span(provenance).first(count),
                        std::span(dirty).first(count));
                }
                builder.appendInterleaved(
                    std::span(interleaved).first(count * shape.channels));
            }
            auto caches = peaks.takeCaches();
            std::vector<std::vector<gui::PeakLevel>> levels;
            for (int c = 0; c < shape.channels; ++c)
            {
                levels.push_back(
                    caches.getCache(c).snapshotBuildState().levels);
            }
            check();
            result.assignRevision(AudioEditRevision::from(
                builder.finish({}, std::make_shared<waveform::SourcePeaks>(
                                       shape, std::move(levels)))));
        }
        else
        {
            Document::AudioSegment segment;
            segment.format = shape.format;
            segment.sampleRate = shape.sampleRate;
            segment.channelCount = shape.channels;
            segment.frameCount = shape.frames;
            segment.samples.resize(shape.channels);
            segment.dirty.resize(shape.channels);
            segment.provenance.resize(shape.channels);
            const auto &revision = clip.getAudioRevision();
            for (int c = 0; c < shape.channels; ++c)
            {
                check();
                segment.samples[c].resize(shape.frames);
                segment.dirty[c].resize(shape.frames);
                segment.provenance[c].resize(shape.frames);
                for (int64_t first = 0; first < shape.frames; first += chunk)
                {
                    check();
                    const auto count = std::min(chunk, shape.frames - first);
                    revision->readChannel(
                        c, first,
                        std::span(segment.samples[c]).subspan(first, count));
                    auto offset = first;
                    revision->visitSourceRanges(
                        c, first, count,
                        [&](const auto &range)
                        {
                            auto provenance =
                                std::span(segment.provenance[c])
                                    .subspan(offset, range.frames);
                            auto dirty = std::span(segment.dirty[c])
                                             .subspan(offset, range.frames);
                            if (range.source)
                            {
                                range.source->readLegacyMetadata(
                                    range.channel, range.start, provenance,
                                    dirty);
                            }
                            else
                            {
                                std::fill(dirty.begin(), dirty.end(), 1);
                                std::fill(provenance.begin(), provenance.end(),
                                          audio::SampleProvenance{});
                            }
                            offset += range.frames;
                        });
                }
            }
            check();
            result.assignSegment(std::move(segment));
        }
        return result;
    }
} // namespace cupuacu::storage
