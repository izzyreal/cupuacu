#pragma once

#include <string>

namespace cupuacu
{
    struct State;
}

namespace cupuacu::audio
{
    struct AudioStreamFailure;
}

namespace cupuacu::actions
{
    bool ensureAudioInputAccess(cupuacu::State *state);
    void reportAudioInputUnavailable(cupuacu::State *state);
    bool setInputMonitoring(cupuacu::State *state, bool enabled);
    void reportInputMonitoringError(cupuacu::State *state,
                                    const std::string &message);
    void
    reportAudioStreamFailure(cupuacu::State *state,
                             const cupuacu::audio::AudioStreamFailure &failure,
                             const std::string &title);
    [[nodiscard]] std::string audioStreamFailureMessage(
        const cupuacu::audio::AudioStreamFailure &failure,
        bool includeWindowsMicrophoneGuidance);
    void handleInputMonitoringProtectionTrip(cupuacu::State *state);
} // namespace cupuacu::actions
