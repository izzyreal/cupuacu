#pragma once

#include "../State.hpp"
#include "OverwritePreservationState.hpp"
#include "wav/WavPreservationSupport.hpp"

#include <string>

namespace cupuacu::file
{
    class OverwritePreservation
    {
    public:
        [[nodiscard]] static OverwritePreservationState
        assessActiveSession(const cupuacu::State *state)
        {
            if (state == nullptr)
            {
                return {.available = false, .reason = "State is null"};
            }

            const auto &session = state->getActiveDocumentSession();
            if (session.currentFile.empty())
            {
                return {.available = false, .reason = "No current file"};
            }
            if (session.overwritePreservationBrokenByOperation)
            {
                return {.available = false,
                        .reason = session.overwritePreservationBrokenReason};
            }

            const auto wavSupport =
                cupuacu::file::wav::WavPreservationSupport::assessOverwrite(state);
            if (wavSupport.supported)
            {
                return {.available = true};
            }

            return {.available = false, .reason = wavSupport.reason};
        }

        static void refreshActiveSession(cupuacu::State *state)
        {
            if (state == nullptr)
            {
                return;
            }
            state->getActiveDocumentSession().overwritePreservation =
                assessActiveSession(state);
        }
    };
} // namespace cupuacu::file
