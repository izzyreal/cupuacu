#include "Monitor.hpp"

#include "../State.hpp"
#include "../gui/OptionsWindow.hpp"
#include "../gui/VuMeterAccess.hpp"
#include "audio/AudioDevices.hpp"

#include <SDL3/SDL.h>

#include <iterator>

#if defined(__APPLE__)
#include "../platform/macos/MicrophonePermission.hpp"
#endif

namespace cupuacu::actions
{
    namespace
    {
        void reportWarning(State *state, const std::string &title,
                           const std::string &message,
                           const bool offerMicrophoneSettings = false)
        {
            if (state && state->errorReporter)
            {
                state->errorReporter(title, message);
                return;
            }
#if defined(_WIN32)
            if (offerMicrophoneSettings)
            {
                constexpr int openSettingsButtonId = 1;
                const SDL_MessageBoxButtonData buttons[] = {
                    {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "OK"},
                    {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,
                     openSettingsButtonId, "Open Microphone Settings"}};
                const SDL_MessageBoxData messageBox{
                    .flags = SDL_MESSAGEBOX_WARNING |
                             SDL_MESSAGEBOX_BUTTONS_RIGHT_TO_LEFT,
                    .window = nullptr,
                    .title = title.c_str(),
                    .message = message.c_str(),
                    .numbuttons = static_cast<int>(std::size(buttons)),
                    .buttons = buttons,
                    .colorScheme = nullptr};
                int selectedButtonId = 0;
                if (SDL_ShowMessageBox(&messageBox, &selectedButtonId) &&
                    selectedButtonId == openSettingsButtonId)
                {
                    SDL_OpenURL("ms-settings:privacy-microphone");
                }
                return;
            }
#else
            (void)offerMicrophoneSettings;
#endif
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, title.c_str(),
                                     message.c_str(), nullptr);
        }

        const char *audioDeviceUnavailableMessage()
        {
#if defined(_WIN32)
            return "Allow desktop apps to access your microphone in Windows "
                   "Settings > Privacy & security > Microphone, and select "
                   "working input and output devices in Options > Audio.";
#else
            return "Select working input and output devices in Options > "
                   "Audio.";
#endif
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

    void reportAudioInputUnavailable(State *state)
    {
        reportWarning(state, "Audio input unavailable",
                      audioDeviceUnavailableMessage(), true);
    }

    void reportInputMonitoringError(State *state, const std::string &message)
    {
        reportWarning(state, "Input monitoring unavailable", message);
    }

    void reportAudioStreamFailure(State *state,
                                  const audio::AudioStreamFailure &failure,
                                  const std::string &title)
    {
        reportWarning(state, title,
                      audio::AudioDevices::describeFailure(failure));
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

        auto *document =
            enabled ? &state->getActiveDocumentSession().document : nullptr;
        const auto result = state->audioDevices->setInputMonitoringEnabled(
            enabled, document, gui::getVuMeterIfPresent(state));
        if (!result)
        {
            if (result.failure)
            {
                reportAudioStreamFailure(state, *result.failure,
                                         "Input monitoring unavailable");
                return false;
            }
            const auto error = state->audioDevices->getInputMonitoringError();
            if (error == audio::InputMonitoringError::SuppressionUnavailable)
            {
                reportInputMonitoringError(
                    state,
                    "Feedback suppression could not be initialized for the "
                    "selected devices.");
            }
            else
            {
                reportWarning(state, "Input monitoring unavailable",
                              audioDeviceUnavailableMessage(), true);
            }
            return false;
        }

        if (enabled)
        {
            gui::dismissOptionsWindow(state);
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
        state->audioDevices->setInputMonitoringEnabled(false, nullptr);
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
