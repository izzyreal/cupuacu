#pragma once

#include "MonitorProtection.hpp"

#include <atomic>
#include <cstdint>

namespace cupuacu::audio
{
    struct AudioDeviceState;
    class AudioDeviceView
    {
    public:
        AudioDeviceView(const AudioDeviceState *,
                        std::atomic<std::uint32_t> *readersToRelease) noexcept;
        ~AudioDeviceView();
        AudioDeviceView(const AudioDeviceView &other) noexcept;
        AudioDeviceView &operator=(const AudioDeviceView &other) noexcept;
        AudioDeviceView(AudioDeviceView &&other) noexcept;
        AudioDeviceView &operator=(AudioDeviceView &&other) noexcept;

        bool isPlaying() const;
        bool isRecording() const;
        bool isInputMonitoringEnabled() const;
        MonitorProtectionTelemetry getMonitorProtectionTelemetry() const;
        int64_t getPlaybackPosition() const;
        int64_t getRecordingPosition() const;

    private:
        void release() noexcept;

        const AudioDeviceState *state = nullptr;
        std::atomic<std::uint32_t> *readers = nullptr;
    };
} // namespace cupuacu::audio
