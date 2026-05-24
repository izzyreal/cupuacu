#pragma once

#include "../State.hpp"
#include "../audio/AudioDevices.hpp"

#include <string>
#include <utility>

namespace cupuacu::actions
{
    struct ActionAvailability
    {
        bool available = true;
        std::string unavailableReason;
    };

    inline ActionAvailability availableAction()
    {
        return {};
    }

    inline ActionAvailability unavailableAction(std::string reason)
    {
        return {.available = false, .unavailableReason = std::move(reason)};
    }

    inline ActionAvailability combineAvailability(ActionAvailability primary,
                                                  ActionAvailability secondary)
    {
        if (!primary.available)
        {
            return primary;
        }
        if (!secondary.available)
        {
            return secondary;
        }
        return availableAction();
    }

    inline bool isPlaybackActive(const cupuacu::State *state)
    {
        return state && state->audioDevices && state->audioDevices->isPlaying();
    }

    inline bool isRecordingActive(const cupuacu::State *state)
    {
        return state && state->audioDevices && state->audioDevices->isRecording();
    }

    inline ActionAvailability describeDocumentMutationAvailability(
        const cupuacu::State *state)
    {
        if (isRecordingActive(state))
        {
            return unavailableAction("Stop recording first");
        }

        if (isPlaybackActive(state))
        {
            return unavailableAction("Stop playback first");
        }

        return availableAction();
    }

    inline bool isDocumentMutationAvailable(const cupuacu::State *state)
    {
        return describeDocumentMutationAvailability(state).available;
    }
} // namespace cupuacu::actions
