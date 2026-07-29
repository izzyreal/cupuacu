#pragma once

#include <cstdint>

namespace cupuacu::audio
{
    enum class MonitorProtectionState : uint8_t
    {
        Inactive,
        WarmingUp,
        Active,
        Decorrelating,
        Tripped,
        Unavailable
    };

    enum class InputMonitoringError : uint8_t
    {
        None,
        DeviceUnavailable,
        SuppressionUnavailable
    };

    struct MonitorProtectionTelemetry
    {
        MonitorProtectionState state = MonitorProtectionState::Inactive;
        uint64_t tripGeneration = 0;
        int estimatedDelayMs = 0;
        float echoReturnLossDb = 0.0f;
        float echoReturnLossEnhancementDb = 0.0f;
    };
} // namespace cupuacu::audio
