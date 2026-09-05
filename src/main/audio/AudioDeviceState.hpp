#pragma once

#include "MonitorProtection.hpp"

#include <cstdint>

namespace cupuacu::audio
{
    struct AudioDeviceState
    {
        bool isPlaying = false;
        bool isRecording = false;
        bool isInputMonitoringEnabled = false;
        MonitorProtectionTelemetry monitorProtection{};
        uint64_t playbackUnderrunFrames = 0;
        int64_t playbackPosition = -1;
        int64_t recordingPosition = -1;
    };
} // namespace cupuacu::audio
