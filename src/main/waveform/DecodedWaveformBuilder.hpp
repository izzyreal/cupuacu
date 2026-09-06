#pragma once

#include "DocumentWaveformCaches.hpp"
#include "../storage/AudioReader.hpp"
#include "../storage/ImportAudioReader.hpp"

namespace cupuacu::waveform
{
    struct DecodedWaveformChunk
    {
        SampleFormat format = SampleFormat::Unknown;
        int sampleRate = 0;
        int64_t frameCount = 0;
        int64_t fromBlock = 0;
        int64_t toBlock = -1;
        std::vector<std::vector<gui::WaveformCache::LevelSpanUpdate>> channels;
        std::optional<DocumentWaveformCaches> cached;
        std::shared_ptr<const storage::ImportAudioReader> audio;
        std::shared_ptr<const class ProgressivePeaks> progressivePeaks;
        std::shared_ptr<const class SourcePeaks> sourcePeaks;
    };

    // The decoder owns both the audio and this builder. Only peak deltas cross
    // threads; neither a full audio snapshot nor another sample pass is needed.
    class DecodedWaveformBuilder
    {
        using Cache = gui::WaveformCache;
        std::vector<Cache::BuildState> channels;
        int64_t builtFrames = 0;
        std::vector<float> samples;

    public:
        std::optional<DecodedWaveformChunk> append(const Document &document,
                                                   int64_t availableFrames)
        {
            auto lease = document.acquireReadLease();
            return appendFrom(
                {lease.getFrameCount(), int(lease.getChannelCount()),
                 lease.getSampleRate(), lease.getSampleFormat()},
                availableFrames,
                [&lease](int channel, int64_t start, std::span<float> output)
                {
                    lease.readChannelFloatBlock(channel, start, output.data(),
                                                output.size());
                });
        }

        template <typename ReadRange>
        std::optional<DecodedWaveformChunk>
        appendFrom(storage::AudioShape shape, int64_t availableFrames,
                   ReadRange read)
        {
            const auto total = shape.frames;
            const auto end = availableFrames == total
                                 ? total
                                 : availableFrames / Cache::BASE_BLOCK_SIZE *
                                       Cache::BASE_BLOCK_SIZE;
            if (end <= builtFrames ||
                (end != total && end - builtFrames < 65536))
            {
                return std::nullopt;
            }
            if (channels.empty())
            {
                for (int64_t c = 0; c < shape.channels; ++c)
                {
                    channels.push_back(Cache::makeFullBuildState(total));
                }
            }
            DecodedWaveformChunk chunk{
                .format = shape.format,
                .sampleRate = shape.sampleRate,
                .frameCount = total,
                .fromBlock = builtFrames / Cache::BASE_BLOCK_SIZE,
                .toBlock = (end - 1) / Cache::BASE_BLOCK_SIZE,
                .channels = {},
                .cached = std::nullopt};
            samples.resize(static_cast<std::size_t>(end - builtFrames));
            for (std::size_t c = 0; c < channels.size(); ++c)
            {
                auto &state = channels[c];
                read(c, builtFrames, std::span<float>(samples));
                CUPUACU_METRIC(
                    performance::add(performance::Work::SampleBytesCopied,
                                     samples.size() * sizeof(float)));
                Cache::rebuildDirtyBlockRangeFromSlice(
                    state.levels, total, chunk.fromBlock, chunk.toBlock,
                    builtFrames, samples.data(), samples.size());
                auto &updates = chunk.channels.emplace_back();
                auto from = chunk.fromBlock;
                auto to = chunk.toBlock;
                for (int level = 0;
                     level < static_cast<int>(state.levels.size()); ++level)
                {
                    Cache::LevelSpanUpdate update{
                        .level = level, .fromIndex = from, .peaks = {}};
                    update.peaks.resize(
                        static_cast<std::size_t>(to - from + 1));
                    std::copy_n(state.levels[level].begin() + from,
                                update.peaks.size(), update.peaks.begin());
                    updates.push_back(std::move(update));
                    from /= 2;
                    to /= 2;
                }
                state.dirtyFromBlock =
                    end == total ? INT64_MAX : chunk.toBlock + 1;
                state.dirtyToBlock = end == total ? -1 : state.dirtyToBlock;
            }
            builtFrames = end;
            return chunk;
        }

        DocumentWaveformCaches takeCaches()
        {
            DocumentWaveformCaches result;
            result.resetToChannelCount(channels.size());
            for (std::size_t c = 0; c < channels.size(); ++c)
            {
                auto &state = channels[c];
                result.getCache(c).applyBuildResult(
                    {state.numSamples, state.dirtyFromBlock, state.dirtyToBlock,
                     std::move(state.levels)});
            }
            return result;
        }
    };
} // namespace cupuacu::waveform
