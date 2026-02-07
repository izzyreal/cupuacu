#pragma once

#include "../../Document.hpp"
#include "../../audio/RecordedChunk.hpp"

#include <algorithm>
#include <cstdint>

namespace cupuacu::actions::audio
{
    struct RecordedChunkApplyResult
    {
        bool channelLayoutChanged = false;
        bool waveformCacheChanged = false;
        int64_t requiredFrameCount = 0;
    };

    inline RecordedChunkApplyResult
    applyRecordedChunk(cupuacu::Document &doc,
                       const cupuacu::audio::RecordedChunk &chunk)
    {
        RecordedChunkApplyResult result{};
        result.requiredFrameCount =
            chunk.startFrame + static_cast<int64_t>(chunk.frameCount);

        const int oldChannelCount = static_cast<int>(doc.getChannelCount());
        const int64_t oldFrameCount = doc.getFrameCount();
        const int chunkChannelCount = static_cast<int>(chunk.channelCount);

        if (doc.getChannelCount() == 0)
        {
            doc.initialize(cupuacu::SampleFormat::FLOAT32, 44100,
                           chunk.channelCount, 0);
        }
        else if (doc.getChannelCount() < chunkChannelCount)
        {
            doc.resizeBuffer(chunkChannelCount, doc.getFrameCount());

            for (int ch = oldChannelCount; ch < chunkChannelCount; ++ch)
            {
                auto &cache = doc.getWaveformCache(ch);
                cache.clear();
                cache.applyInsert(0, doc.getFrameCount());
                if (doc.getFrameCount() > 0)
                {
                    cache.invalidateSamples(0, doc.getFrameCount() - 1);
                }
            }
            result.waveformCacheChanged = true;
        }

        const int64_t appendCount =
            std::max<int64_t>(0, result.requiredFrameCount - oldFrameCount);
        if (appendCount > 0)
        {
            doc.insertFrames(oldFrameCount, appendCount);
            result.waveformCacheChanged = true;
        }

        const int64_t overwriteStart =
            std::clamp<int64_t>(chunk.startFrame, 0, oldFrameCount);
        const int64_t overwriteEnd =
            std::min<int64_t>(result.requiredFrameCount, oldFrameCount) - 1;
        if (overwriteEnd >= overwriteStart)
        {
            for (int ch = 0; ch < doc.getChannelCount(); ++ch)
            {
                doc.getWaveformCache(ch).invalidateSamples(overwriteStart,
                                                           overwriteEnd);
            }
            result.waveformCacheChanged = true;
        }

        result.channelLayoutChanged = oldChannelCount != doc.getChannelCount();

        for (uint32_t frame = 0; frame < chunk.frameCount; ++frame)
        {
            const int64_t writeFrame = chunk.startFrame + frame;
            const std::size_t base =
                static_cast<std::size_t>(frame) *
                cupuacu::audio::kMaxRecordedChannels;

            doc.setSample(0, writeFrame, chunk.interleavedSamples[base]);
            if (doc.getChannelCount() > 1)
            {
                doc.setSample(1, writeFrame, chunk.interleavedSamples[base + 1]);
            }
        }

        return result;
    }
} // namespace cupuacu::actions::audio
