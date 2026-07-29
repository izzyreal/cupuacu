#pragma once

#include <string>

namespace cupuacu
{
    struct State;
}

namespace cupuacu::actions
{
    bool ensureAudioInputAccess(cupuacu::State *state);
    bool setInputMonitoring(cupuacu::State *state, bool enabled);
    void reportInputMonitoringError(cupuacu::State *state,
                                    const std::string &message);
} // namespace cupuacu::actions
