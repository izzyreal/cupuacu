#pragma once

#include "MonitorCancellationBackend.hpp"
#include "MonitorProtection.hpp"

#include <cstdint>
#include <memory>

namespace cupuacu::audio
{
    struct MonitorStreamFormat
    {
        int sampleRate = 0;
        unsigned long callbackFrames = 0;
        uint8_t inputChannels = 0;
        uint8_t outputChannels = 0;
        double inputLatencySeconds = 0.0;
        double outputLatencySeconds = 0.0;
    };

    struct AudioCallbackTiming
    {
        double inputAdcTime = 0.0;
        double currentTime = 0.0;
        double outputDacTime = 0.0;
        bool valid = false;
        bool discontinuity = false;
    };

    struct MonitorProcessResult
    {
        bool producedOutput = false;
        MonitorProtectionTelemetry telemetry{};
    };

    class InputMonitorPipeline
    {
    public:
        InputMonitorPipeline();
        ~InputMonitorPipeline();

        InputMonitorPipeline(const InputMonitorPipeline &) = delete;
        InputMonitorPipeline &operator=(const InputMonitorPipeline &) = delete;

        bool
        prepare(const MonitorStreamFormat &format,
                std::unique_ptr<MonitorCancellationBackend> backend = nullptr);
        bool isPrepared() const noexcept;

        void startSession() noexcept;
        void suspendSession() noexcept;

        MonitorProcessResult
        process(const float *input, float *stereoOutput, unsigned long frames,
                const AudioCallbackTiming &timing) noexcept;

        MonitorProtectionTelemetry getTelemetry() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace cupuacu::audio
