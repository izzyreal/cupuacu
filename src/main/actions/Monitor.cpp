#include "Monitor.hpp"

#include "../State.hpp"
#include "../gui/VuMeterAccess.hpp"
#include "audio/AudioDevices.hpp"

#include <SDL3/SDL.h>

#if defined(__APPLE__)
#include "../platform/macos/MicrophonePermission.hpp"
#endif

namespace cupuacu::actions
{
    namespace
    {
        void reportWarning(State *state, const std::string &title,
                           const std::string &message)
        {
            if (state && state->errorReporter)
            {
                state->errorReporter(title, message);
                return;
            }
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, title.c_str(),
                                     message.c_str(), nullptr);
        }
    } // namespace

    bool ensureAudioInputAccess(State *state)
    {
#if defined(__APPLE__)
        if (!cupuacu::platform::macos::ensureMicrophoneAccess())
        {
            reportWarning(state, "Microphone access required",
                          "Allow microphone access for Cupuacu in System "
                          "Settings to use audio input.");
            return false;
        }
#else
        (void)state;
#endif
        return true;
    }

    void reportInputMonitoringError(State *state, const std::string &message)
    {
        reportWarning(state, "Input monitoring unavailable", message);
    }

    bool setInputMonitoring(State *state, const bool enabled)
    {
        if (!state || !state->audioDevices)
        {
            return false;
        }

        if (enabled)
        {
            if (state->audioDevices->isPlaying())
            {
                return false;
            }
            if (!ensureAudioInputAccess(state))
            {
                return false;
            }
        }

        if (!state->audioDevices->setInputMonitoringEnabled(
                enabled, gui::getVuMeterIfPresent(state)))
        {
            const auto error = state->audioDevices->getInputMonitoringError();
            reportInputMonitoringError(
                state,
                error == audio::InputMonitoringError::SuppressionUnavailable
                    ? "Feedback suppression could not be initialized for the "
                      "selected devices."
                    : "Select working input and output devices in Options > "
                      "Audio.");
            return false;
        }

        if (!enabled && !state->audioDevices->isPlaying() &&
            !state->audioDevices->isRecording())
        {
            gui::startVuMeterDecay(state);
        }
        return true;
    }

    void handleInputMonitoringProtectionTrip(State *state)
    {
        if (!state || !state->audioDevices)
        {
            return;
        }

        const auto telemetry =
            state->audioDevices->getMonitorProtectionTelemetry();
        state->audioDevices->setInputMonitoringEnabled(false);
        if (!state->audioDevices->isPlaying() &&
            !state->audioDevices->isRecording())
        {
            gui::startVuMeterDecay(state);
        }

        reportWarning(
            state,
            telemetry.state == audio::MonitorProtectionState::Unavailable
                ? "Input monitoring unavailable"
                : "Feedback protection stopped monitoring",
            telemetry.state == audio::MonitorProtectionState::Unavailable
                ? "Feedback suppression stopped unexpectedly. Select other "
                  "audio devices or restart Cupuacu before monitoring."
                : "Reduce the speaker level or use headphones, then re-enable "
                  "Monitor.");
    }
} // namespace cupuacu::actions
