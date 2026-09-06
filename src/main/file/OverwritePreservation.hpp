#pragma once

#include "../State.hpp"
#include "AudioExport.hpp"
#include "PreservationBackend.hpp"
#include "OverwritePreservationState.hpp"

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
            if (session.preservationReferenceFile.empty() &&
                session.currentFile.empty())
            {
                return {.available = false, .reason = "No preservation reference"};
            }
            if (session.overwritePreservationBrokenByOperation)
            {
                return {.available = false,
                        .reason = session.overwritePreservationBrokenReason};
            }

            auto settings = session.currentFileExportSettings;
            if (!settings.has_value())
            {
                settings = cupuacu::file::defaultExportSettingsForPath(
                    session.currentFile, session.document.getSampleFormat());
            }
            if (!settings.has_value())
            {
                return {.available = false,
                        .reason = "Could not determine current file export settings"};
            }

            const auto support =
                cupuacu::file::assessPreservationOverwrite(state, *settings);
            if (support.available)
            {
                return {.available = true};
            }

            return {.available = false, .reason = support.reason};
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

        static void refreshSession(cupuacu::State *state, const int tabIndex)
        {
            if (state == nullptr || tabIndex < 0 ||
                tabIndex >= static_cast<int>(state->tabs.size()))
            {
                return;
            }

            const int previousActiveTabIndex = state->activeTabIndex;
            state->activeTabIndex = tabIndex;
            refreshActiveSession(state);
            state->activeTabIndex = previousActiveTabIndex;
        }
    };
} // namespace cupuacu::file
