#pragma once

#include <cstdint>

namespace cupuacu::audio
{
    struct AudioDeviceState
    {
        bool isPlaying = false;
        bool isRecording = false;
        int64_t playbackPosition = -1;
        int64_t recordingPosition = -1;
    };
} // namespace cupuacu::audio
