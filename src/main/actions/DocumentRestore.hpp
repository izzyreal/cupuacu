#pragma once

#include "../State.hpp"
#include "../gui/MainViewAccess.hpp"
#include "../gui/Waveform.hpp"
#include "DocumentIo.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace cupuacu::actions
{
    namespace detail
    {
        struct DocumentRestoreFailure
        {
            std::string path;
            std::string reason;
        };

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
                if (state->mainWindowInitialFrameRendered &&
                    SDL_WasInit(SDL_INIT_VIDEO) != 0)
                {
                    SDL_ShowSimpleMessageBox(
                        SDL_MESSAGEBOX_WARNING, title.c_str(),
                        message.c_str(), getDocumentIoParentWindow(state));
                    return;
                }
                state->pendingStartupWarning = std::make_pair(title, message);
                return;
            }
        }

        inline bool hasRestorableDocument(
            const cupuacu::persistence::PersistedOpenDocumentState &documentState)
        {
            if (!documentState.autosaveSnapshotPath.empty() &&
                std::filesystem::exists(documentState.autosaveSnapshotPath))
            {
                return true;
            }
            return !documentState.filePath.empty() &&
                   std::filesystem::exists(documentState.filePath);
        }

        inline std::string restoreIdentityPath(
            const cupuacu::persistence::PersistedOpenDocumentState &documentState)
        {
            if (!documentState.filePath.empty())
            {
                return documentState.filePath;
            }
            return documentState.autosaveSnapshotPath;
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
                if (detail::hasRestorableDocument(documentState))
                {
                    plan.openDocuments.push_back(documentState);
                    plan.openFiles.push_back(
                        detail::restoreIdentityPath(documentState));
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
        const cupuacu::persistence::PersistedSessionState &persistedSessionState,
        const bool useAsyncFileRestore = false)
    {
        if (!state)
        {
            return;
        }

        const auto plan = planStartupDocumentRestore(
            persistedRecentFiles, persistedSessionState);
        state->recentFiles = plan.recentFiles;
        state->snapEnabled = persistedSessionState.snapEnabled;

        const bool hasAutosaveSnapshotRestore = std::any_of(
            plan.openDocuments.begin(), plan.openDocuments.end(),
            [](const auto &documentState)
            {
                return !documentState.autosaveSnapshotPath.empty() &&
                       std::filesystem::exists(
                           documentState.autosaveSnapshotPath);
            });
        if (useAsyncFileRestore && !plan.openFiles.empty() &&
            !hasAutosaveSnapshotRestore)
        {
            state->startupRestore = cupuacu::StartupRestoreStatus{
                .active = true,
                .remaining = static_cast<int>(plan.openFiles.size()),
                .activeOpenFileIndex = plan.activeOpenFileIndex,
                .restoredActiveTabIndex = -1,
                .shouldPersistState = plan.shouldPersistState,
                .failures = {},
            };
            for (size_t index = 0; index < plan.openFiles.size(); ++index)
            {
                state->pendingOpenFiles.push_back(cupuacu::PendingOpenRequest{
                    .kind = cupuacu::PendingOpenKind::StartupRestore,
                    .path = plan.openFiles[index],
                    .targetTabIndex = static_cast<int>(index),
                    .updateRecentFiles = false,
                    .persistedDocumentState = plan.openDocuments[index],
                });
            }
            return;
        }

        if (!plan.openFiles.empty())
        {
            int restoredActiveOpenFileIndex = -1;
            std::vector<detail::DocumentRestoreFailure> restoreFailures;
            for (size_t index = 0; index < plan.openFiles.size(); ++index)
            {
                std::string failureReason;
                bool loaded = false;
                const auto &documentState = plan.openDocuments[index];
                if (!documentState.autosaveSnapshotPath.empty() &&
                    std::filesystem::exists(documentState.autosaveSnapshotPath))
                {
                    if (index == 0 || prepareTabForOpenedDocument(state))
                    {
                        cupuacu::LongTaskScope longTask(
                            state, "Restoring document",
                            documentState.filePath.empty()
                                ? "Unsaved document"
                                : documentState.filePath);
                        prepareForDocumentTransition(state);
                        loaded =
                            cupuacu::persistence::loadDocumentAutosaveSnapshot(
                                documentState.autosaveSnapshotPath,
                                state->getActiveDocumentSession());
                        if (loaded)
                        {
                            refreshDocumentUi(state);
                            setMainWindowTitle(
                                state,
                                state->getActiveDocumentSession().currentFile.empty()
                                    ? kUntitledDocumentTitle
                                    : state->getActiveDocumentSession().currentFile);
                        }
                        else
                        {
                            failureReason = "Autosave snapshot could not be read.";
                        }
                    }
                }
                else
                {
                    loaded =
                        (index == 0)
                            ? loadFileIntoSession(
                                  state, documentState.filePath, false,
                                  false, false, false, &failureReason)
                            : loadFileIntoNewTab(
                                  state, documentState.filePath, false,
                                  false, false, false, &failureReason);
                }
                if (!loaded)
                {
                    detail::reportDocumentIoFailure(state, "Open",
                                                    plan.openFiles[index],
                                                    failureReason, false);
                    restoreFailures.push_back(
                        {plan.openFiles[index],
                         detail::condensedRestoreReason(plan.openFiles[index],
                                                        failureReason)});
                    state->recentFiles.erase(
                        std::remove(state->recentFiles.begin(),
                                    state->recentFiles.end(),
                                    plan.openFiles[index]),
                        state->recentFiles.end());
                    continue;
                }

                applyPersistedOpenDocumentState(state, documentState);

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
                    state,
                    state->getActiveDocumentSession().currentFile.empty()
                        ? kUntitledDocumentTitle
                        : state->getActiveDocumentSession().currentFile);
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
} // namespace cupuacu::actions
