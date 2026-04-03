#pragma once

#include "../Document.hpp"
#include "../SelectedChannels.hpp"
#include "AudioProcessor.hpp"
#include "RecordedChunk.hpp"

#include <cstdint>

namespace cupuacu::audio::callback_core
{
    struct StereoMeterLevels
    {
        float peakLeft = 0.0f;
        float peakRight = 0.0f;
        float rmsLeft = 0.0f;
        float rmsRight = 0.0f;
    };

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
                          uint64_t &playbackPendingEndPos, bool &isPlaying,
                          float *out, unsigned long framesPerBuffer,
                          StereoMeterLevels &meterLevels,
                          const cupuacu::audio::AudioProcessor *processor = nullptr,
                          uint64_t effectStartPos = 0,
                          uint64_t effectEndPos = 0,
                          cupuacu::SelectedChannels processorChannels =
                              cupuacu::SelectedChannels::BOTH);

    void recordInputIntoChunks(const float *input, unsigned long framesPerBuffer,
                               uint8_t recordingChannels,
                               int64_t &recordingPosition, void *chunkSinkUser,
                               ChunkPushFn chunkPushFn,
                               StereoMeterLevels &meterLevels);
} // namespace cupuacu::audio::callback_core
