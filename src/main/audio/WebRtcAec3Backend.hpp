#pragma once

#include "MonitorCancellationBackend.hpp"
#include "MonitorProtection.hpp"

#include <memory>

namespace cupuacu::audio
{
    class WebRtcAec3Backend final : public MonitorCancellationBackend
    {
    public:
        explicit WebRtcAec3Backend(
            FeedbackSuppressionMode mode = FeedbackSuppressionMode::Smooth);
        ~WebRtcAec3Backend() override;

        WebRtcAec3Backend(const WebRtcAec3Backend &) = delete;
        WebRtcAec3Backend &operator=(const WebRtcAec3Backend &) = delete;

        bool prepare(uint8_t captureChannels) override;

        bool process(MonitorProcessingFrame &capture,
                     const MonitorProcessingFrame &renderReference, int delayMs,
                     bool echoPathChanged, bool feedbackRisk,
                     MonitorCancellationMetrics &metrics) noexcept override;

    private:
        FeedbackSuppressionMode mode;
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace cupuacu::audio
