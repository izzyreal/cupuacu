#pragma once

#include "../Document.hpp"
#include "../Paths.hpp"
#include "../SampleFormat.hpp"
#include "../State.hpp"
#include "../file/file_loading.hpp"
#include "../gui/MainViewAccess.hpp"
#include "../gui/Waveform.hpp"
#include "../persistence/RecentFilesPersistence.hpp"
#include "Play.hpp"
#include "Zoom.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace cupuacu::actions
{
    constexpr const char *kUntitledDocumentTitle = "Untitled";

    struct StartupDocumentRestorePlan
    {
        std::string fileToOpen;
        std::vector<std::string> recentFiles;
        bool shouldPersistRecentFiles = false;
    };

    inline int sampleFormatBitDepth(const cupuacu::SampleFormat format)
    {
        switch (format)
        {
            case cupuacu::SampleFormat::PCM_S8:
                return 8;
            case cupuacu::SampleFormat::PCM_S16:
                return 16;
            case cupuacu::SampleFormat::PCM_S24:
                return 24;
            case cupuacu::SampleFormat::PCM_S32:
            case cupuacu::SampleFormat::FLOAT32:
                return 32;
            case cupuacu::SampleFormat::FLOAT64:
                return 64;
            case cupuacu::SampleFormat::Unknown:
            default:
                return 0;
        }
    }

    inline std::string sampleFormatLabel(const cupuacu::SampleFormat format)
    {
        const int bitDepth = sampleFormatBitDepth(format);
        if (bitDepth == 0)
        {
            return "";
        }
        return std::to_string(bitDepth) + "-bit";
    }

    inline cupuacu::SampleFormat sampleFormatForBitDepth(const int bitDepth)
    {
        switch (bitDepth)
        {
            case 8:
                return cupuacu::SampleFormat::PCM_S8;
            case 16:
            default:
                return cupuacu::SampleFormat::PCM_S16;
        }
    }

    inline bool hasActiveDocument(const cupuacu::State *state)
    {
        return state &&
               state->activeDocumentSession.document.getChannelCount() > 0;
    }

    inline void persistRecentFiles(cupuacu::State *state)
    {
        if (!state || !state->paths)
        {
            return;
        }

        cupuacu::persistence::RecentFilesPersistence::save(
            state->paths->recentlyOpenedFilesPath(), state->recentFiles);
    }

    inline void removeRecentFile(cupuacu::State *state,
                                 const std::string &absolutePath)
    {
        if (!state || absolutePath.empty())
        {
            return;
        }

        auto &recentFiles = state->recentFiles;
        recentFiles.erase(
            std::remove(recentFiles.begin(), recentFiles.end(), absolutePath),
            recentFiles.end());
        persistRecentFiles(state);
    }

    inline void rememberRecentFile(cupuacu::State *state,
                                   const std::string &absolutePath)
    {
        if (!state || absolutePath.empty())
        {
            return;
        }

        auto &recentFiles = state->recentFiles;
        recentFiles.erase(
            std::remove(recentFiles.begin(), recentFiles.end(), absolutePath),
            recentFiles.end());
        recentFiles.insert(recentFiles.begin(), absolutePath);
        if (recentFiles.size() >
            cupuacu::persistence::RecentFilesPersistence::kMaxEntries)
        {
            recentFiles.resize(
                cupuacu::persistence::RecentFilesPersistence::kMaxEntries);
        }
        persistRecentFiles(state);
    }

    inline void setMainWindowTitle(cupuacu::State *state,
                                   const std::string &title)
    {
        if (!state || !state->mainDocumentSessionWindow)
        {
            return;
        }

        auto *mainWindow = state->mainDocumentSessionWindow->getWindow();
        if (!mainWindow || !mainWindow->getSdlWindow())
        {
            return;
        }

        SDL_SetWindowTitle(mainWindow->getSdlWindow(), title.c_str());
    }

    inline void prepareForDocumentTransition(cupuacu::State *state)
    {
        if (!state)
        {
            return;
        }

        if (state->audioDevices)
        {
            if (state->audioDevices->isPlaying() ||
                state->audioDevices->isRecording())
            {
                requestStop(state);
            }
            state->audioDevices->clearRecordedChunks();
        }

        state->undoables.clear();
        state->redoables.clear();
    }

    inline void refreshDocumentUi(cupuacu::State *state)
    {
        if (!state)
        {
            return;
        }

        gui::rebuildMainWaveforms(state);
        resetWaveformState(state);
        resetSampleValueUnderMouseCursor(state);
        resetZoom(state);
        gui::Waveform::updateAllSamplePoints(state);
        gui::Waveform::setAllWaveformsDirty(state);
        gui::requestMainViewRefresh(state);
    }

    inline void closeCurrentDocument(cupuacu::State *state)
    {
        if (!state)
        {
            return;
        }

        prepareForDocumentTransition(state);

        auto &session = state->activeDocumentSession;
        session.currentFile.clear();
        session.document.initialize(cupuacu::SampleFormat::Unknown, 0, 0, 0);
        session.selection.reset();
        session.cursor = 0;
        session.syncSelectionAndCursorToDocumentLength();

        refreshDocumentUi(state);
        setMainWindowTitle(state, kUntitledDocumentTitle);
    }

    inline void createNewDocument(cupuacu::State *state, const int sampleRate,
                                  const cupuacu::SampleFormat format,
                                  const int channels = 2)
    {
        if (!state)
        {
            return;
        }

        prepareForDocumentTransition(state);

        auto &session = state->activeDocumentSession;
        session.currentFile.clear();
        session.document.initialize(format, sampleRate, channels, 0);
        session.selection.reset();
        session.cursor = 0;
        session.syncSelectionAndCursorToDocumentLength();

        refreshDocumentUi(state);
        setMainWindowTitle(state, kUntitledDocumentTitle);
    }

    inline bool loadFileIntoSession(cupuacu::State *state,
                                    const std::string &absoluteFilePath,
                                    const bool updateRecentFiles = true)
    {
        if (!state || absoluteFilePath.empty())
        {
            return false;
        }

        prepareForDocumentTransition(state);
        state->activeDocumentSession.currentFile = absoluteFilePath;
        cupuacu::file::loadSampleData(state);
        refreshDocumentUi(state);
        setMainWindowTitle(state, absoluteFilePath);

        if (updateRecentFiles)
        {
            rememberRecentFile(state, absoluteFilePath);
        }

        return true;
    }

    inline StartupDocumentRestorePlan planStartupDocumentRestore(
        const std::vector<std::string> &recentFiles)
    {
        StartupDocumentRestorePlan plan{};

        if (!recentFiles.empty() &&
            std::filesystem::exists(recentFiles.front()))
        {
            plan.fileToOpen = recentFiles.front();
            plan.recentFiles = recentFiles;
            return plan;
        }

        plan.recentFiles.reserve(recentFiles.size());
        for (const auto &path : recentFiles)
        {
            if (!path.empty() && std::filesystem::exists(path))
            {
                plan.recentFiles.push_back(path);
            }
        }
        plan.shouldPersistRecentFiles = plan.recentFiles != recentFiles;
        return plan;
    }

    inline void restoreStartupDocument(cupuacu::State *state)
    {
        if (!state)
        {
            return;
        }

        const auto plan = planStartupDocumentRestore(state->recentFiles);
        state->recentFiles = plan.recentFiles;
        if (!plan.fileToOpen.empty())
        {
            loadFileIntoSession(state, plan.fileToOpen, false);
            return;
        }

        if (plan.shouldPersistRecentFiles)
        {
            persistRecentFiles(state);
        }
        state->activeDocumentSession.currentFile.clear();
    }

    inline void requestExit(cupuacu::State *state)
    {
        SDL_Event quitEvent{};
        quitEvent.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quitEvent);
    }
} // namespace cupuacu::actions
