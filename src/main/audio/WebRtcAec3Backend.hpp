#pragma once

#include "MonitorCancellationBackend.hpp"

#include <memory>

namespace cupuacu::audio
{
    class WebRtcAec3Backend final : public MonitorCancellationBackend
    {
    public:
        WebRtcAec3Backend();
        ~WebRtcAec3Backend() override;

        WebRtcAec3Backend(const WebRtcAec3Backend &) = delete;
        WebRtcAec3Backend &operator=(const WebRtcAec3Backend &) = delete;

        bool prepare(uint8_t captureChannels) override;

        bool process(MonitorProcessingFrame &capture,
                     const MonitorProcessingFrame &renderReference, int delayMs,
                     bool echoPathChanged,
                     MonitorCancellationMetrics &metrics) noexcept override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace cupuacu::audio
