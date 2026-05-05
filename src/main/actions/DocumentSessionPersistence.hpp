#pragma once

#include "../SampleFormat.hpp"
#include "../State.hpp"
#include "../persistence/DocumentAutosave.hpp"
#include "../persistence/RecentFilesPersistence.hpp"
#include "../persistence/SessionStatePersistence.hpp"
#include "../undo/UndoManifestPersistence.hpp"
#include "SessionWindowGeometryPlanning.hpp"
#include "io/BackgroundSave.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace cupuacu::actions
{
    namespace detail
    {
        inline std::filesystem::path makeAutosaveSnapshotPath(
            const cupuacu::State *state)
        {
            static std::atomic<uint64_t> counter{0};

            if (!state || !state->paths)
            {
                return {};
            }

            const auto tick = static_cast<unsigned long long>(
                std::chrono::steady_clock::now().time_since_epoch().count());
            const auto index = counter.fetch_add(1);
            return state->paths->autosavePath() /
                   ("document-" + std::to_string(tick) + "-" +
                    std::to_string(index) + ".cupuacu-autosave");
        }

        inline std::filesystem::path makeUndoStorePath(
            const cupuacu::State *state, const uint64_t tabId)
        {
            static std::atomic<uint64_t> counter{0};

            if (!state || !state->paths)
            {
                return {};
            }

            const auto tick = static_cast<unsigned long long>(
                std::chrono::steady_clock::now().time_since_epoch().count());
            const auto index = counter.fetch_add(1);
            return state->paths->undoPath() /
                   ("tab-" + std::to_string(tabId) + "-" +
                    std::to_string(tick) + "-" + std::to_string(index));
        }

        inline void discardAutosaveSnapshot(cupuacu::DocumentSession &session)
        {
            cupuacu::persistence::removeDocumentAutosaveSnapshot(
                session.autosaveSnapshotPath);
            session.clearAutosaveSnapshotReference();
        }

        inline void discardUndoStore(cupuacu::DocumentSession &session)
        {
            session.undoStore.clear();
        }

        inline void ensureUndoStoreForTab(cupuacu::State *state, const int tabIndex)
        {
            if (!state || tabIndex < 0 ||
                tabIndex >= static_cast<int>(state->tabs.size()))
            {
                return;
            }

            auto &tab = state->tabs[static_cast<std::size_t>(tabIndex)];
            if (tab.session.undoStore.isAttached())
            {
                return;
            }

            const auto path = makeUndoStorePath(state, tab.id);
            if (path.empty())
            {
                return;
            }
            tab.session.undoStore.attach(path);
        }
    } // namespace detail

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

    inline cupuacu::persistence::PersistedSessionState
    buildPersistedOpenSessionState(const cupuacu::State *state)
    {
        cupuacu::persistence::PersistedSessionState persisted{};
        if (!state)
        {
            return persisted;
        }

        int openFileIndex = 0;
        if (state->mainDocumentSessionWindow)
        {
            auto *mainWindow = state->mainDocumentSessionWindow->getWindow();
            auto *sdlWindow = mainWindow ? mainWindow->getSdlWindow() : nullptr;
            int windowWidth = 0;
            int windowHeight = 0;
            int windowX = 0;
            int windowY = 0;
            if (sdlWindow &&
                SDL_GetWindowSize(sdlWindow, &windowWidth, &windowHeight) &&
                windowWidth > 0 && windowHeight > 0)
            {
                if (SDL_GetWindowPosition(sdlWindow, &windowX, &windowY))
                {
                    applyPersistedWindowGeometry(
                        persisted, windowWidth, windowHeight, windowX, windowY);
                }
                else
                {
                    applyPersistedWindowGeometry(persisted, windowWidth,
                                                 windowHeight, std::nullopt,
                                                 std::nullopt);
                }
            }
        }

        for (int i = 0; i < static_cast<int>(state->tabs.size()); ++i)
        {
            const auto &tab = state->tabs[static_cast<size_t>(i)];
            if (tab.session.currentFile.empty() &&
                tab.session.autosaveSnapshotPath.empty())
            {
                continue;
            }

            cupuacu::persistence::PersistedOpenDocumentState documentState{};
            documentState.filePath = tab.session.currentFile;
            documentState.autosaveSnapshotPath =
                tab.session.autosaveSnapshotPath.string();
            if (!tab.session.undoStore.root().empty())
            {
                const auto manifestPath =
                    cupuacu::undo::manifestPathForStore(tab.session.undoStore.root());
                if (cupuacu::undo::saveUndoManifest(manifestPath, tab))
                {
                    documentState.undoStorePath =
                        tab.session.undoStore.root().string();
                }
            }
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
            if (!tab.session.currentFile.empty())
            {
                persisted.openFiles.push_back(tab.session.currentFile);
            }
            if (i == state->activeTabIndex)
            {
                persisted.activeOpenFileIndex = openFileIndex;
            }
            ++openFileIndex;
        }

        persisted.snapEnabled = state->snapEnabled;

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
        const auto persisted = buildPersistedOpenSessionState(state);
        cupuacu::persistence::SessionStatePersistence::save(
            state->paths->sessionStatePath(), persisted);
    }

    inline void autosaveDocumentAfterMutation(cupuacu::State *state,
                                              const int tabIndex)
    {
        if (!state || !state->paths || tabIndex < 0 ||
            tabIndex >= static_cast<int>(state->tabs.size()))
        {
            return;
        }

        auto &tab = state->tabs[static_cast<std::size_t>(tabIndex)];
        auto &session = tab.session;
        const auto &document = session.document;
        if (document.getChannelCount() <= 0)
        {
            return;
        }
        if (!session.currentFile.empty() && tab.undoables.empty())
        {
            if (!session.autosaveSnapshotPath.empty())
            {
                detail::discardAutosaveSnapshot(session);
                persistSessionState(state);
            }
            return;
        }
        if (session.autosavedWaveformDataVersion ==
                document.getWaveformDataVersion() &&
            session.autosavedMarkerDataVersion == document.getMarkerDataVersion() &&
            !session.autosaveSnapshotPath.empty())
        {
            return;
        }
        if (session.autosaveSnapshotPath.empty())
        {
            session.autosaveSnapshotPath =
                detail::makeAutosaveSnapshotPath(state);
        }
        if (session.autosaveSnapshotPath.empty())
        {
            return;
        }

        cupuacu::actions::io::queueAutosaveForTab(state, tabIndex);
    }

    inline void autosaveActiveDocumentAfterMutation(cupuacu::State *state)
    {
        if (!state)
        {
            return;
        }
        autosaveDocumentAfterMutation(state, state->activeTabIndex);
    }

    inline void clearActiveDocumentAutosave(cupuacu::State *state)
    {
        if (!state)
        {
            return;
        }

        detail::discardAutosaveSnapshot(state->getActiveDocumentSession());
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
} // namespace cupuacu::actions
