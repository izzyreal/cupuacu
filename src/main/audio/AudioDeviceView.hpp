#pragma once

#include "MonitorProtection.hpp"

#include <cstdint>

namespace cupuacu::audio
{
    struct AudioDeviceState;
    class AudioDeviceView
    {
    public:
        explicit AudioDeviceView(const AudioDeviceState *) noexcept;

        bool isPlaying() const;
        bool isRecording() const;
        bool isInputMonitoringEnabled() const;
        MonitorProtectionTelemetry getMonitorProtectionTelemetry() const;
        int64_t getPlaybackPosition() const;
        int64_t getRecordingPosition() const;

    private:
        const AudioDeviceState *state;
    };
} // namespace cupuacu::audio
