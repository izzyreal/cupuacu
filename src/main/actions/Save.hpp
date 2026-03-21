#pragma once

#include "../State.hpp"
#include "../file/AudioExport.hpp"
#include "../file/AudioFileWriter.hpp"
#include "../file/WavWriter.hpp"
#include "DocumentLifecycle.hpp"

namespace cupuacu::actions
{
    static void saveAs(cupuacu::State *state, const std::string &absoluteFilePath,
                       const file::AudioExportSettings &settings);

    static void overwrite(cupuacu::State *state)
    {
        if (!state)
        {
            return;
        }

        auto &session = state->getActiveDocumentSession();
        if (session.currentFile.empty())
        {
            return;
        }

        auto settings = session.currentFileExportSettings;
        if (!settings.has_value())
        {
            settings = file::defaultExportSettingsForPath(
                session.currentFile, session.document.getSampleFormat());
        }
        if (!settings.has_value())
        {
            return;
        }

        if (file::isOverwritePreservingWavRewriteCandidate(*settings))
        {
            file::WavWriter::rewriteWavFile(state);
            return;
        }

        file::AudioFileWriter::writeFile(state, session.currentFile, *settings);
    }

    static void saveAs(cupuacu::State *state, const std::string &absoluteFilePath)
    {
        if (!state || absoluteFilePath.empty())
        {
            return;
        }

        const auto settings = file::defaultExportSettingsForPath(
            absoluteFilePath, state->getActiveDocumentSession().document.getSampleFormat());
        if (!settings.has_value())
        {
            return;
        }

        saveAs(state, absoluteFilePath, *settings);
    }

    static void saveAs(cupuacu::State *state, const std::string &absoluteFilePath,
                       const file::AudioExportSettings &settings)
    {
        if (!state || absoluteFilePath.empty() || !settings.isValid())
        {
            return;
        }

        const auto normalizedPath =
            file::normalizeExportPath(absoluteFilePath, settings);
        file::AudioFileWriter::writeFile(state, normalizedPath, settings);
        state->getActiveDocumentSession().setCurrentFile(normalizedPath.string(),
                                                         settings);
        rememberRecentFile(state, normalizedPath.string());
        setMainWindowTitle(state, normalizedPath.string());
    }
} // namespace cupuacu::actions
