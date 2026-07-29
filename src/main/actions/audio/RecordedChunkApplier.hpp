#pragma once

#include "../../DocumentSession.hpp"
#include "../../audio/RecordedChunk.hpp"
#include "../../file/OverwritePreservationMutation.hpp"

#include <algorithm>
#include <cstdint>

namespace cupuacu::actions::audio
{
    struct RecordedChunkApplyResult
    {
        bool channelLayoutChanged = false;
        bool waveformCacheChanged = false;
        int64_t requiredFrameCount = 0;
        cupuacu::file::OverwritePreservationMutation preservationMutation;
    };

    inline RecordedChunkApplyResult
    applyRecordedChunk(cupuacu::DocumentSession &session,
                       const cupuacu::audio::RecordedChunk &chunk)
    {
        auto &doc = session.document;
        RecordedChunkApplyResult result{};
        session.stopWaveformCacheBuild();
        result.requiredFrameCount =
            chunk.startFrame + static_cast<int64_t>(chunk.frameCount);

        const int oldChannelCount = static_cast<int>(doc.getChannelCount());
        const int64_t oldFrameCount = doc.getFrameCount();
        const int chunkChannelCount = static_cast<int>(chunk.channelCount);

        if (doc.getChannelCount() == 0)
        {
            doc.initialize(cupuacu::SampleFormat::FLOAT32, 44100,
                           chunk.channelCount, 0);
            result.preservationMutation =
                cupuacu::file::OverwritePreservationMutationHelper::incompatible(
                    "Recording changed sample format");
        }
        else if (doc.getChannelCount() < chunkChannelCount)
        {
            doc.resizeBuffer(chunkChannelCount, doc.getFrameCount());
            result.preservationMutation =
                cupuacu::file::OverwritePreservationMutationHelper::incompatible(
                    "Recording changed channel count");

            for (int ch = oldChannelCount; ch < chunkChannelCount; ++ch)
            {
                auto &cache = session.getWaveformCache(ch);
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
                session.getWaveformCache(ch).invalidateSamples(overwriteStart,
                                                               overwriteEnd);
            }
            result.waveformCacheChanged = true;
        }

        result.channelLayoutChanged = oldChannelCount != doc.getChannelCount();

        // RecordedChunk always uses a two-float frame stride, including for a
        // mono input. Document clamps the writable channel count, allowing the
        // complete chunk to be committed under one document lock/version bump.
        doc.writeInterleavedFloatBlock(
            chunk.startFrame, chunk.interleavedSamples.data(), chunk.frameCount,
            cupuacu::audio::kMaxRecordedChannels, true);

        return result;
    }
} // namespace cupuacu::actions::audio
