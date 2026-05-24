#pragma once

#include "../State.hpp"
#include "../file/AudioExport.hpp"

#include <optional>

namespace cupuacu::actions
{
    inline std::optional<cupuacu::file::AudioExportSettings>
    preferredExportAudioDialogSettings(const cupuacu::State *state)
    {
        if (!state)
        {
            return std::nullopt;
        }

        if (const auto *tab = state->getActiveTab();
            tab && tab->lastExportAudioDialogSettings.has_value())
        {
            return tab->lastExportAudioDialogSettings;
        }

        const auto &session = state->getActiveDocumentSession();
        if (session.currentFileExportSettings.has_value())
        {
            return session.currentFileExportSettings;
        }
        if (session.currentFile.empty())
        {
            return std::nullopt;
        }

        return cupuacu::file::defaultExportSettingsForPath(
            session.currentFile, session.document.getSampleFormat());
    }

    inline void rememberLastUsedExportAudioDialogSettings(
        cupuacu::State *state, const cupuacu::file::AudioExportSettings &settings)
    {
        if (!state || !state->getActiveTab())
        {
            return;
        }

        state->getActiveTab()->lastExportAudioDialogSettings = settings;
    }
} // namespace cupuacu::actions
