#pragma once

#include "../actions/DocumentLifecycle.hpp"
#include "../file/SaveWritePlan.hpp"
#include "Menu.hpp"

#include "../State.hpp"
#include <string>

namespace cupuacu::gui
{
#ifdef __APPLE__
    inline std::string menuBarPrimaryShortcut(const std::string &key)
    {
        return " (Cmd + " + key + ")";
    }

    inline std::string menuBarRedoShortcut()
    {
        return " (Cmd + Shift + Z)";
    }
#else
    inline std::string menuBarPrimaryShortcut(const std::string &key)
    {
        return " (Ctrl + " + key + ")";
    }

    inline std::string menuBarRedoShortcut()
    {
        return " (Ctrl + Shift + Z)";
    }
#endif

    inline std::string buildUndoMenuLabel(cupuacu::State *state)
    {
        auto description = state->getUndoDescription();
        if (!description.empty())
        {
            description.insert(0, " ");
        }
        return "Undo" + description + menuBarPrimaryShortcut("Z");
    }

    inline std::string buildRedoMenuLabel(cupuacu::State *state)
    {
        auto description = state->getRedoDescription();
        if (!description.empty())
        {
            description.insert(0, " ");
        }
        return "Redo" + description + menuBarRedoShortcut();
    }

    inline std::string buildOverwriteMenuLabel(cupuacu::State *state)
    {
        return "Overwrite" + menuBarPrimaryShortcut("S");
    }

    inline std::string buildPreservingOverwriteMenuLabel()
    {
        return "Preserving overwrite";
    }

    inline std::string buildSaveAsTooltipText()
    {
        return "Write the active document to a new file and make that file the "
               "current document path.";
    }

    inline std::string buildPreservingSaveAsTooltipText()
    {
        return "Write a new file in preservation mode, keeping unchanged "
               "source WAV PCM16 bytes intact where possible and using the "
               "latest opened or saved file as the reference.";
    }

    inline std::string buildOverwriteTooltipText()
    {
        return "Rewrite the current file at its existing path using the "
               "current export settings.";
    }

    inline std::string buildPreservingOverwriteTooltipText()
    {
        return "Rewrite the current file in preservation mode, keeping "
               "unchanged source WAV PCM16 bytes intact where possible.";
    }

    inline MenuAvailability
    describeSaveAsAvailability(const cupuacu::State *state)
    {
        if (!actions::hasActiveDocument(state))
        {
            return {.available = false,
                    .unavailableReason = "No document is open"};
        }

        return {};
    }

    inline MenuAvailability
    describePreservingSaveAsAvailability(const cupuacu::State *state)
    {
        if (!actions::hasActiveDocument(state))
        {
            return {.available = false,
                    .unavailableReason = "No document is open"};
        }

        const auto &session = state->getActiveDocumentSession();
        if (session.preservationReferenceFile.empty())
        {
            return {.available = false,
                    .unavailableReason =
                        "No preservation reference file is available yet"};
        }

        auto settings = session.currentFileExportSettings;
        if (!settings.has_value())
        {
            settings = cupuacu::file::defaultExportSettingsForPath(
                session.preservationReferenceFile.empty()
                    ? session.currentFile
                    : session.preservationReferenceFile,
                session.document.getSampleFormat());
        }
        if (!settings.has_value())
        {
            return {
                .available = false,
                .unavailableReason =
                    "The preservation reference file format is not writable"};
        }

        const auto plan = cupuacu::file::SaveWritePlanner::planPreservingSaveAs(
            state, *settings);
        return {.available =
                    plan.mode ==
                    cupuacu::file::SaveWriteMode::OverwritePreservingRewrite,
                .unavailableReason =
                    plan.preservationUnavailableReason.value_or("")};
    }

    inline MenuAvailability
    describeOverwriteAvailability(const cupuacu::State *state)
    {
        if (!actions::hasActiveDocument(state))
        {
            return {.available = false,
                    .unavailableReason = "No document is open"};
        }

        if (state->getActiveDocumentSession().currentFile.empty())
        {
            return {.available = false,
                    .unavailableReason =
                        "The active document does not have a current file path "
                        "yet"};
        }

        return {};
    }

    inline MenuAvailability
    describePreservingOverwriteAvailability(const cupuacu::State *state)
    {
        const auto overwriteAvailability = describeOverwriteAvailability(state);
        if (!overwriteAvailability.available)
        {
            return overwriteAvailability;
        }

        const auto &session = state->getActiveDocumentSession();
        auto settings = session.currentFileExportSettings;
        if (!settings.has_value())
        {
            settings = cupuacu::file::defaultExportSettingsForPath(
                session.currentFile, session.document.getSampleFormat());
        }
        if (!settings.has_value())
        {
            return {.available = false,
                    .unavailableReason =
                        "The current file format is not writable"};
        }

        const auto plan =
            cupuacu::file::SaveWritePlanner::planPreservingOverwrite(state,
                                                                     *settings);
        return {.available =
                    plan.mode ==
                    cupuacu::file::SaveWriteMode::OverwritePreservingRewrite,
                .unavailableReason =
                    plan.preservationUnavailableReason.value_or("")};
    }

    inline bool isSelectionEditAvailable(const cupuacu::State *state)
    {
        return state->getActiveDocumentSession().selection.isActive();
    }

    inline bool isPasteAvailable(const cupuacu::State *state)
    {
        return state->clipboard.getFrameCount() > 0;
    }
} // namespace cupuacu::gui
