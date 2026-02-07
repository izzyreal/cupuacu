#pragma once

#include "../Document.hpp"
#include "../SelectedChannels.hpp"
#include "RecordedChunk.hpp"

#include <cstdint>

namespace cupuacu::audio::callback_core
{
    using ChunkPushFn = bool (*)(void *userdata,
                                 const cupuacu::audio::RecordedChunk &chunk);

    void writeSilenceToOutput(float *out, unsigned long frames);

    bool fillOutputBuffer(const cupuacu::Document *document,
                          bool selectionIsActive,
                          cupuacu::SelectedChannels selectedChannels,
                          int64_t &playbackPosition, uint64_t &playbackStartPos,
                          uint64_t &playbackEndPos, bool playbackLoopEnabled,
                          bool &playbackHasPendingSwitch,
                          uint64_t &playbackPendingStartPos,
                          uint64_t &playbackPendingEndPos, bool &isPlaying, float *out,
                          unsigned long framesPerBuffer, float &peakLeft,
                          float &peakRight);

    void recordInputIntoChunks(const float *input, unsigned long framesPerBuffer,
                               uint8_t recordingChannels,
                               int64_t &recordingPosition, void *chunkSinkUser,
                               ChunkPushFn chunkPushFn, float &peakLeft,
                               float &peakRight);
} // namespace cupuacu::audio::callback_core
