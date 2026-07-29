#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace cupuacu::audio
{
    inline constexpr int kMonitorProcessingSampleRate = 48000;
    inline constexpr std::size_t kMonitorProcessingFrameCount = 480;
    inline constexpr std::size_t kMonitorMaxChannels = 2;

    using MonitorProcessingChannel =
        std::array<float, kMonitorProcessingFrameCount>;
    using MonitorProcessingFrame =
        std::array<MonitorProcessingChannel, kMonitorMaxChannels>;

    struct MonitorCancellationMetrics
    {
        float echoReturnLossDb = 0.0f;
        float echoReturnLossEnhancementDb = 0.0f;
        int delayMs = 0;
        bool active = false;
    };

    class MonitorCancellationBackend
    {
    public:
        virtual ~MonitorCancellationBackend() = default;

        virtual bool prepare(uint8_t captureChannels) = 0;

        virtual bool process(MonitorProcessingFrame &capture,
                             const MonitorProcessingFrame &renderReference,
                             int delayMs, bool echoPathChanged,
                             MonitorCancellationMetrics &metrics) noexcept = 0;
    };
} // namespace cupuacu::audio
