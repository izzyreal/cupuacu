#pragma once

#include "../Document.hpp"
#include "../Paths.hpp"
#include "../SampleFormat.hpp"
#include "../State.hpp"
#include "../file/file_loading.hpp"
#include "../gui/MainViewAccess.hpp"
#include "../gui/NewFileDialogWindow.hpp"
#include "../gui/Waveform.hpp"
#include "../persistence/RecentFilesPersistence.hpp"
#include "../persistence/SessionStatePersistence.hpp"
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
        std::vector<std::string> recentFiles;
        std::vector<std::string> openFiles;
        int activeOpenFileIndex = -1;
        bool shouldPersistState = false;
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
               state->getActiveDocumentSession().document.getChannelCount() > 0;
    }

    inline cupuacu::persistence::PersistedSessionState
    buildPersistedOpenSessionState(const cupuacu::State *state)
    {
        cupuacu::persistence::PersistedSessionState persisted{};
        if (!state)
        {
            return persisted;
        }

        int openFileIndex = 0;
        for (int i = 0; i < static_cast<int>(state->tabs.size()); ++i)
        {
            const auto &tab = state->tabs[static_cast<size_t>(i)];
            if (tab.session.currentFile.empty())
            {
                continue;
            }

            persisted.openFiles.push_back(tab.session.currentFile);
            if (i == state->activeTabIndex)
            {
                persisted.activeOpenFileIndex = openFileIndex;
            }
            ++openFileIndex;
        }

        return persisted;
    }

    inline void persistSessionState(cupuacu::State *state)
    {
        if (!state || !state->paths)
        {
            return;
        }

        cupuacu::persistence::RecentFilesPersistence::save(
            state->paths->recentlyOpenedFilesPath(), state->recentFiles);
        cupuacu::persistence::SessionStatePersistence::save(
            state->paths->sessionStatePath(),
            buildPersistedOpenSessionState(state));
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
        persistSessionState(state);
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
        persistSessionState(state);
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

        state->getActiveUndoables().clear();
        state->getActiveRedoables().clear();
    }

    inline void bindMainWindowToActiveDocument(cupuacu::State *state)
    {
        if (!state || !state->mainDocumentSessionWindow)
        {
            return;
        }

        state->mainDocumentSessionWindow->bindDocumentSession(
            &state->getActiveDocumentSession(), &state->getActiveViewState());
    }

    inline bool shouldReuseLoneBlankTab(const cupuacu::State *state)
    {
        if (!state || state->tabs.size() != 1 || state->activeTabIndex != 0)
        {
            return false;
        }

        const auto *tab = state->getActiveTab();
        if (!tab)
        {
            return false;
        }

        return tab->session.currentFile.empty() &&
               tab->session.document.getChannelCount() == 0 &&
               tab->session.document.getFrameCount() == 0 &&
               tab->undoables.empty() && tab->redoables.empty();
    }

    inline bool prepareTabForOpenedDocument(cupuacu::State *state)
    {
        if (!state)
        {
            return false;
        }

        if (state->audioDevices &&
            (state->audioDevices->isPlaying() ||
             state->audioDevices->isRecording()))
        {
            return false;
        }

        if (!shouldReuseLoneBlankTab(state))
        {
            state->tabs.emplace_back();
            state->activeTabIndex = static_cast<int>(state->tabs.size()) - 1;
        }

        bindMainWindowToActiveDocument(state);
        resetSampleValueUnderMouseCursor(state);
        return true;
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

    inline void closeCurrentDocument(cupuacu::State *state,
                                     const bool shouldPersistState = true)
    {
        if (!state)
        {
            return;
        }

        prepareForDocumentTransition(state);

        auto &session = state->getActiveDocumentSession();
        session.currentFile.clear();
        session.document.initialize(cupuacu::SampleFormat::Unknown, 0, 0, 0);
        session.selection.reset();
        session.cursor = 0;
        session.syncSelectionAndCursorToDocumentLength();

        refreshDocumentUi(state);
        setMainWindowTitle(state, kUntitledDocumentTitle);
        if (shouldPersistState)
        {
            persistSessionState(state);
        }
    }

    inline void createNewDocument(cupuacu::State *state, const int sampleRate,
                                  const cupuacu::SampleFormat format,
                                  const int channels = 2,
                                  const bool shouldPersistState = true)
    {
        if (!state)
        {
            return;
        }

        prepareForDocumentTransition(state);

        auto &session = state->getActiveDocumentSession();
        session.currentFile.clear();
        session.document.initialize(format, sampleRate, channels, 0);
        session.selection.reset();
        session.cursor = 0;
        session.syncSelectionAndCursorToDocumentLength();

        refreshDocumentUi(state);
        setMainWindowTitle(state, kUntitledDocumentTitle);
        if (shouldPersistState)
        {
            persistSessionState(state);
        }
    }

    inline bool createNewDocumentInNewTab(cupuacu::State *state,
                                          const int sampleRate,
                                          const cupuacu::SampleFormat format,
                                          const int channels = 2,
                                          const bool shouldPersistState = true)
    {
        if (!prepareTabForOpenedDocument(state))
        {
            return false;
        }

        createNewDocument(state, sampleRate, format, channels,
                          shouldPersistState);
        return true;
    }

    inline bool loadFileIntoSession(cupuacu::State *state,
                                    const std::string &absoluteFilePath,
                                    const bool updateRecentFiles = true,
                                    const bool shouldPersistState = true)
    {
        if (!state || absoluteFilePath.empty())
        {
            return false;
        }

        prepareForDocumentTransition(state);
        state->getActiveDocumentSession().currentFile = absoluteFilePath;
        cupuacu::file::loadSampleData(state);
        refreshDocumentUi(state);
        setMainWindowTitle(state, absoluteFilePath);

        if (updateRecentFiles)
        {
            rememberRecentFile(state, absoluteFilePath);
        }
        else if (shouldPersistState)
        {
            persistSessionState(state);
        }

        return true;
    }

    inline bool loadFileIntoNewTab(cupuacu::State *state,
                                   const std::string &absoluteFilePath,
                                   const bool updateRecentFiles = true,
                                   const bool shouldPersistState = true)
    {
        if (!prepareTabForOpenedDocument(state))
        {
            return false;
        }

        return loadFileIntoSession(state, absoluteFilePath, updateRecentFiles,
                                   shouldPersistState);
    }

    inline StartupDocumentRestorePlan planStartupDocumentRestore(
        const std::vector<std::string> &persistedRecentFiles,
        const cupuacu::persistence::PersistedSessionState &persistedSessionState)
    {
        StartupDocumentRestorePlan plan{};
        plan.activeOpenFileIndex = persistedSessionState.activeOpenFileIndex;

        plan.recentFiles.reserve(persistedRecentFiles.size());
        for (const auto &path : persistedRecentFiles)
        {
            if (!path.empty() && std::filesystem::exists(path))
            {
                plan.recentFiles.push_back(path);
            }
        }

        plan.openFiles.reserve(persistedSessionState.openFiles.size());
        for (const auto &path : persistedSessionState.openFiles)
        {
            if (!path.empty() && std::filesystem::exists(path))
            {
                plan.openFiles.push_back(path);
            }
        }

        if (plan.openFiles.empty())
        {
            plan.activeOpenFileIndex = -1;
        }
        else
        {
            const int normalizedIndex =
                std::clamp(plan.activeOpenFileIndex, 0,
                           static_cast<int>(plan.openFiles.size()) - 1);
            if (normalizedIndex != plan.activeOpenFileIndex)
            {
                plan.shouldPersistState = true;
            }
            plan.activeOpenFileIndex = normalizedIndex;
        }

        if (plan.recentFiles != persistedRecentFiles ||
            plan.openFiles != persistedSessionState.openFiles)
        {
            plan.shouldPersistState = true;
        }

        return plan;
    }

    inline void restoreStartupDocument(
        cupuacu::State *state,
        const std::vector<std::string> &persistedRecentFiles,
        const cupuacu::persistence::PersistedSessionState &persistedSessionState)
    {
        if (!state)
        {
            return;
        }

        const auto plan = planStartupDocumentRestore(
            persistedRecentFiles, persistedSessionState);
        state->recentFiles = plan.recentFiles;

        if (!plan.openFiles.empty())
        {
            loadFileIntoSession(state, plan.openFiles.front(), false, false);

            for (size_t index = 1; index < plan.openFiles.size(); ++index)
            {
                loadFileIntoNewTab(state, plan.openFiles[index], false, false);
            }

            if (plan.activeOpenFileIndex >= 0 &&
                plan.activeOpenFileIndex < static_cast<int>(state->tabs.size()))
            {
                state->activeTabIndex = plan.activeOpenFileIndex;
                bindMainWindowToActiveDocument(state);
                refreshDocumentUi(state);
                setMainWindowTitle(
                    state, state->getActiveDocumentSession().currentFile);
            }

            if (plan.shouldPersistState)
            {
                persistSessionState(state);
            }
            return;
        }

        if (plan.shouldPersistState)
        {
            persistSessionState(state);
        }
        state->getActiveDocumentSession().currentFile.clear();
    }

    inline void showNewFileDialog(cupuacu::State *state)
    {
        if (!state)
        {
            return;
        }

        if (!state->newFileDialogWindow || !state->newFileDialogWindow->isOpen())
        {
            state->newFileDialogWindow.reset(new gui::NewFileDialogWindow(state));
        }
        else
        {
            state->newFileDialogWindow->raise();
        }
    }

    inline void requestExit(cupuacu::State *state)
    {
        SDL_Event quitEvent{};
        quitEvent.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quitEvent);
    }
} // namespace cupuacu::actions
