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

        // feedbackRisk is the preceding monitor frame's detector result. It
        // lets a backend arm conservative artifact controls without treating
        // ordinary quiet echo cancellation as feedback.
        virtual bool process(MonitorProcessingFrame &capture,
                             const MonitorProcessingFrame &renderReference,
                             int delayMs, bool echoPathChanged,
                             bool feedbackRisk,
                             MonitorCancellationMetrics &metrics) noexcept = 0;
    };
} // namespace cupuacu::audio
