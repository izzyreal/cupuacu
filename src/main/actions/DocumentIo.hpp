#pragma once

#include "../LongTask.hpp"
#include "../State.hpp"
#include "../file/AudioFileLoading.hpp"
#include "../file/OwnedAudioImport.hpp"
#include "../file/OverwritePreservation.hpp"
#include "DocumentUi.hpp"

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <exception>
#include <string>

namespace cupuacu::actions
{
    namespace detail
    {
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
            catch (const cupuacu::LongTaskCanceledError &)
            {
                if (failureReason != nullptr)
                {
                    *failureReason = "Operation canceled";
                }
                return false;
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
    } // namespace detail

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
                cupuacu::LongTaskScope longTask(
                    state, "Opening file", absoluteFilePath, std::nullopt,
                    true, true);
                prepareForDocumentTransition(state);
                state->getActiveDocumentSession().setCurrentFile(absoluteFilePath);
                const auto root = state->paths
                                      ? state->paths->statePath()
                                      : std::filesystem::temp_directory_path();
                static std::atomic<uint64_t> importSequence{0};
                const auto directory = root /
                    ("import-sync-" + std::to_string(
                        std::chrono::steady_clock::now().time_since_epoch().count()) +
                     "-" + std::to_string(importSequence.fetch_add(1)));
                auto imported = file::importOwnedAudio(
                    absoluteFilePath, directory,
                    storage::defaultDecodedBlockCache(),
                    [state](const auto &detail, auto progress)
                    {
                        updateLongTask(state, detail, progress, true);
                    },
                    [state] { return isLongTaskCancelRequested(state); }, {},
                    {.preferFilesystemClone = true,
                     .waveformCacheRoot = state->paths
                         ? state->paths->waveformCachePath()
                         : std::filesystem::path{}});
                imported.metadata.audioRevision =
                    storage::AudioEditRevision::from(imported.audio);
                imported.metadata.ownedSource = std::move(imported.audio);
                file::commitLoadedAudioFile(
                    state->getActiveDocumentSession(), absoluteFilePath,
                    std::move(imported.metadata), state->paths.get());
                file::OverwritePreservation::refreshActiveSession(state);
                refreshDocumentUi(state);
                setMainWindowTitleToActiveDocument(state);
            });
        if (!loaded)
        {
            state->tabs[static_cast<size_t>(activeTabIndex)] = previousTab;
            bindMainWindowToActiveDocument(state);
            refreshDocumentUi(state);
            setMainWindowTitleToActiveDocument(state);
            return false;
        }

        auto previousSession = previousTab.session;
        detail::discardAutosaveSnapshot(previousSession);
        detail::discardUndoStore(previousSession);
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
        setMainWindowTitleToActiveDocument(state);
        return false;
    }
} // namespace cupuacu::actions
