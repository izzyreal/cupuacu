#pragma once

#include "../Document.hpp"
#include "../Paths.hpp"
#include "../SampleFormat.hpp"
#include "../State.hpp"
#include "../file/file_loading.hpp"
#include "../gui/MainViewAccess.hpp"
#include "../gui/NewFileDialogWindow.hpp"
#include "../gui/ExportAudioDialogWindow.hpp"
#include "../gui/Waveform.hpp"
#include "../persistence/RecentFilesPersistence.hpp"
#include "../persistence/SessionStatePersistence.hpp"
#include "Play.hpp"
#include "ViewPolicy.hpp"
#include "Zoom.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

namespace cupuacu::actions
{
    constexpr const char *kUntitledDocumentTitle = "Untitled";

    namespace detail
    {
        struct DocumentRestoreFailure
        {
            std::string path;
            std::string reason;
        };

        inline SDL_Window *getDocumentIoParentWindow(cupuacu::State *state)
        {
            if (!state)
            {
                return nullptr;
            }
            if (state->modalWindow && state->modalWindow->isOpen())
            {
                return state->modalWindow->getSdlWindow();
            }
            if (state->mainDocumentSessionWindow &&
                state->mainDocumentSessionWindow->getWindow() &&
                state->mainDocumentSessionWindow->getWindow()->isOpen())
            {
                return state->mainDocumentSessionWindow->getWindow()->getSdlWindow();
            }
            return nullptr;
        }

        inline std::string formatDocumentIoFailureMessage(const char *operation,
                                                          const std::string &path,
                                                          const std::string &reason)
        {
            std::string message = std::string(operation) + " failed";
            if (!path.empty())
            {
                message += " for:\n" + path;
            }
            if (!reason.empty())
            {
                message += "\n\n" + reason;
            }
            return message;
        }

        inline void reportDocumentIoFailure(cupuacu::State *state,
                                            const char *operation,
                                            const std::string &path,
                                            const std::string &reason,
                                            const bool showUi)
        {
            const std::string message =
                formatDocumentIoFailureMessage(operation, path, reason);

            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", message.c_str());
            if (state && state->errorReporter)
            {
                state->errorReporter(
                    std::string(operation) + " failed", message);
                return;
            }
            if (!showUi || SDL_WasInit(SDL_INIT_VIDEO) == 0)
            {
                return;
            }

            const std::string title = std::string(operation) + " failed";
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(),
                                     message.c_str(),
                                     getDocumentIoParentWindow(state));
        }

        template <typename Fn>
        inline bool runDocumentIoOperation(cupuacu::State *state,
                                           const char *operation,
                                           const std::string &path,
                                           const bool showUi,
                                           const bool reportFailure,
                                           std::string *failureReason, Fn &&fn)
        {
            try
            {
                fn();
                return true;
            }
            catch (const std::exception &e)
            {
                if (failureReason != nullptr)
                {
                    *failureReason = e.what();
                }
                if (reportFailure)
                {
                    reportDocumentIoFailure(state, operation, path, e.what(), showUi);
                }
                return false;
            }
            catch (...)
            {
                constexpr const char *unknownError = "An unknown error occurred.";
                if (failureReason != nullptr)
                {
                    *failureReason = unknownError;
                }
                if (reportFailure)
                {
                    reportDocumentIoFailure(state, operation, path,
                                            unknownError, showUi);
                }
                return false;
            }
        }

        inline std::string buildRestoreSummaryMessage(
            const std::vector<DocumentRestoreFailure> &failures)
        {
            std::string message = "Cupuacu could not reopen some previously open files.";
            if (failures.empty())
            {
                return message;
            }

            message += "\n\n";
            const std::size_t maxVisibleFailures = 5;
            const std::size_t visibleFailures =
                std::min(failures.size(), maxVisibleFailures);
            for (std::size_t index = 0; index < visibleFailures; ++index)
            {
                message += failures[index].path;
                if (!failures[index].reason.empty())
                {
                    message += "\n" + failures[index].reason;
                }
                if (index + 1 < visibleFailures)
                {
                    message += "\n\n";
                }
            }

            if (failures.size() > visibleFailures)
            {
                message += "\n\n";
                message += std::to_string(failures.size() - visibleFailures);
                message += " more file(s) could not be reopened. See the log file for details.";
            }

            return message;
        }

        inline std::string condensedRestoreReason(const std::string &path,
                                                  const std::string &reason)
        {
            const std::string openPrefix = "Failed to open file: " + path + ": ";
            if (reason.rfind(openPrefix, 0) == 0)
            {
                return reason.substr(openPrefix.size());
            }

            const std::string readPrefix =
                "Failed to read samples from file: " + path + ": ";
            if (reason.rfind(readPrefix, 0) == 0)
            {
                return reason.substr(readPrefix.size());
            }

            return reason;
        }

        inline void reportStartupRestoreFailures(
            cupuacu::State *state,
            const std::vector<DocumentRestoreFailure> &failures)
        {
            if (failures.empty())
            {
                return;
            }

            const std::string title = "Some files could not be reopened";
            const std::string message = buildRestoreSummaryMessage(failures);
            if (state && state->errorReporter)
            {
                state->errorReporter(title, message);
                return;
            }
            if (state)
            {
                state->pendingStartupWarning = std::make_pair(title, message);
                return;
            }
        }
    } // namespace detail

    struct StartupDocumentRestorePlan
    {
        std::vector<std::string> recentFiles;
        std::vector<cupuacu::persistence::PersistedOpenDocumentState>
            openDocuments;
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

            cupuacu::persistence::PersistedOpenDocumentState documentState{};
            documentState.filePath = tab.session.currentFile;
            documentState.samplesPerPixel = tab.viewState.samplesPerPixel;
            documentState.sampleOffset = tab.viewState.sampleOffset;
            documentState.cursor = tab.session.cursor;
            if (tab.session.selection.isActive())
            {
                documentState.selectionStart = tab.session.selection.getStartInt();
                documentState.selectionEndExclusive =
                    tab.session.selection.getEndExclusiveInt();
            }
            for (const auto &marker : tab.session.document.getMarkers())
            {
                documentState.markers.push_back(
                    cupuacu::persistence::PersistedDocumentMarker{
                        .id = marker.id,
                        .frame = marker.frame,
                        .label = marker.label,
                    });
            }

            persisted.openDocuments.push_back(documentState);
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

    inline void refreshBoundDocumentUi(cupuacu::State *state)
    {
        if (!state)
        {
            return;
        }

        gui::rebuildMainWaveforms(state);
        resetSampleValueUnderMouseCursor(state);
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
        session.clearCurrentFile();
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
        session.clearCurrentFile();
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
                                    const bool shouldPersistState = true,
                                    const bool showUiOnFailure = true,
                                    const bool reportFailure = true,
                                    std::string *failureReason = nullptr)
    {
        if (!state || absoluteFilePath.empty())
        {
            return false;
        }

        const int activeTabIndex = state->activeTabIndex;
        const auto previousTab = state->tabs[static_cast<size_t>(activeTabIndex)];

        const bool loaded = detail::runDocumentIoOperation(
            state, "Open", absoluteFilePath, showUiOnFailure, reportFailure,
            failureReason,
            [&]
            {
                prepareForDocumentTransition(state);
                state->getActiveDocumentSession().setCurrentFile(absoluteFilePath);
                cupuacu::file::loadSampleData(state);
                refreshDocumentUi(state);
                setMainWindowTitle(state, absoluteFilePath);
            });
        if (!loaded)
        {
            state->tabs[static_cast<size_t>(activeTabIndex)] = previousTab;
            bindMainWindowToActiveDocument(state);
            refreshDocumentUi(state);
            setMainWindowTitle(
                state,
                state->getActiveDocumentSession().currentFile.empty()
                    ? kUntitledDocumentTitle
                    : state->getActiveDocumentSession().currentFile);
            return false;
        }

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
                                   const bool shouldPersistState = true,
                                   const bool showUiOnFailure = true,
                                   const bool reportFailure = true,
                                   std::string *failureReason = nullptr)
    {
        if (!state)
        {
            return false;
        }

        const auto originalTabCount = state->tabs.size();
        const int originalActiveTabIndex = state->activeTabIndex;

        if (!prepareTabForOpenedDocument(state))
        {
            return false;
        }

        const bool loaded = loadFileIntoSession(
            state, absoluteFilePath, updateRecentFiles, shouldPersistState,
            showUiOnFailure, reportFailure, failureReason);
        if (loaded)
        {
            return true;
        }

        if (state->tabs.size() > originalTabCount)
        {
            state->tabs.erase(state->tabs.begin() +
                              static_cast<std::ptrdiff_t>(originalTabCount));
        }
        state->activeTabIndex = std::clamp(
            originalActiveTabIndex, 0, static_cast<int>(state->tabs.size()) - 1);
        bindMainWindowToActiveDocument(state);
        refreshDocumentUi(state);
        setMainWindowTitle(
            state,
            state->getActiveDocumentSession().currentFile.empty()
                ? kUntitledDocumentTitle
                : state->getActiveDocumentSession().currentFile);
        return false;
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

        if (!persistedSessionState.openDocuments.empty())
        {
            plan.openDocuments.reserve(persistedSessionState.openDocuments.size());
            plan.openFiles.reserve(persistedSessionState.openDocuments.size());
            for (const auto &documentState : persistedSessionState.openDocuments)
            {
                if (!documentState.filePath.empty() &&
                    std::filesystem::exists(documentState.filePath))
                {
                    plan.openDocuments.push_back(documentState);
                    plan.openFiles.push_back(documentState.filePath);
                }
            }
        }
        else
        {
            plan.openDocuments.reserve(persistedSessionState.openFiles.size());
            plan.openFiles.reserve(persistedSessionState.openFiles.size());
            for (const auto &path : persistedSessionState.openFiles)
            {
                if (!path.empty() && std::filesystem::exists(path))
                {
                    plan.openDocuments.push_back(
                        cupuacu::persistence::PersistedOpenDocumentState{
                            .filePath = path,
                        });
                    plan.openFiles.push_back(path);
                }
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

    inline void applyPersistedOpenDocumentState(
        cupuacu::State *state,
        const cupuacu::persistence::PersistedOpenDocumentState &documentState)
    {
        if (!state)
        {
            return;
        }

        auto &session = state->getActiveDocumentSession();
        auto &viewState = state->getActiveViewState();
        const int64_t frameCount = session.document.getFrameCount();

        session.selection.reset();
        session.syncSelectionAndCursorToDocumentLength();
        session.cursor = std::clamp(documentState.cursor.value_or(0),
                                    int64_t{0}, frameCount);
        if (documentState.selectionStart.has_value() &&
            documentState.selectionEndExclusive.has_value())
        {
            const int64_t start = std::clamp(*documentState.selectionStart,
                                             int64_t{0}, frameCount);
            const int64_t endExclusive = std::clamp(
                *documentState.selectionEndExclusive, int64_t{0}, frameCount);
            if (endExclusive > start)
            {
                session.selection.setValue1(start);
                session.selection.setValue2(endExclusive);
            }
        }
        std::vector<cupuacu::DocumentMarker> markers;
        markers.reserve(documentState.markers.size());
        for (const auto &marker : documentState.markers)
        {
            markers.push_back(cupuacu::DocumentMarker{
                .id = marker.id,
                .frame = marker.frame,
                .label = marker.label,
            });
        }
        session.document.replaceMarkers(std::move(markers));

        if (documentState.samplesPerPixel.has_value() &&
            *documentState.samplesPerPixel > 0.0)
        {
            viewState.samplesPerPixel = *documentState.samplesPerPixel;
            const int64_t sampleOffset =
                std::max<int64_t>(0, documentState.sampleOffset.value_or(0));
            if (state->mainDocumentSessionWindow)
            {
                updateSampleOffset(state, sampleOffset);
                applyDurationChangeViewPolicy(state);
            }
            else
            {
                viewState.sampleOffset = sampleOffset;
            }
        }
        else if (state->mainDocumentSessionWindow)
        {
            resetZoom(state);
        }

        gui::Waveform::updateAllSamplePoints(state);
        gui::Waveform::setAllWaveformsDirty(state);
        gui::requestMainViewRefresh(state);
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
            int restoredActiveOpenFileIndex = -1;
            std::vector<detail::DocumentRestoreFailure> restoreFailures;
            for (size_t index = 0; index < plan.openFiles.size(); ++index)
            {
                std::string failureReason;
                const bool loaded =
                    (index == 0)
                        ? loadFileIntoSession(
                              state, plan.openDocuments[index].filePath, false,
                              false, false, false, &failureReason)
                        : loadFileIntoNewTab(
                              state, plan.openDocuments[index].filePath, false,
                              false, false, false, &failureReason);
                if (!loaded)
                {
                    detail::reportDocumentIoFailure(state, "Open",
                                                    plan.openDocuments[index].filePath,
                                                    failureReason, false);
                    restoreFailures.push_back(
                        {plan.openDocuments[index].filePath,
                         detail::condensedRestoreReason(plan.openDocuments[index].filePath,
                                                        failureReason)});
                    state->recentFiles.erase(
                        std::remove(state->recentFiles.begin(),
                                    state->recentFiles.end(),
                                    plan.openDocuments[index].filePath),
                        state->recentFiles.end());
                    continue;
                }

                applyPersistedOpenDocumentState(state, plan.openDocuments[index]);

                if (static_cast<int>(index) == plan.activeOpenFileIndex)
                {
                    restoredActiveOpenFileIndex =
                        static_cast<int>(state->tabs.size()) - 1;
                }
            }

            if (restoredActiveOpenFileIndex >= 0 &&
                restoredActiveOpenFileIndex < static_cast<int>(state->tabs.size()))
            {
                state->activeTabIndex = restoredActiveOpenFileIndex;
                bindMainWindowToActiveDocument(state);
                refreshBoundDocumentUi(state);
                setMainWindowTitle(
                    state, state->getActiveDocumentSession().currentFile);
            }

            if (plan.shouldPersistState)
            {
                persistSessionState(state);
            }
            detail::reportStartupRestoreFailures(state, restoreFailures);
            if (!state->getActiveDocumentSession().currentFile.empty())
            {
                return;
            }
        }

        state->getActiveDocumentSession().clearCurrentFile();
        if (plan.shouldPersistState || !plan.openFiles.empty())
        {
            persistSessionState(state);
        }
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

    inline void showExportAudioDialog(
        cupuacu::State *state,
        const cupuacu::PendingSaveAsMode mode =
            cupuacu::PendingSaveAsMode::Generic)
    {
        if (!state)
        {
            return;
        }

        state->pendingSaveAsMode = mode;

        if (!state->exportAudioDialogWindow ||
            !state->exportAudioDialogWindow->isOpen())
        {
            state->exportAudioDialogWindow.reset(
                new gui::ExportAudioDialogWindow(state));
        }
        else
        {
            state->exportAudioDialogWindow->raise();
        }
    }

    inline void requestExit(cupuacu::State *state)
    {
        SDL_Event quitEvent{};
        quitEvent.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quitEvent);
    }
} // namespace cupuacu::actions
