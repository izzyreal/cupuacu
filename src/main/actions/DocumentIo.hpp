#pragma once

#include "../LongTask.hpp"
#include "../State.hpp"
#include "../file/file_loading.hpp"
#include "DocumentUi.hpp"

#include <SDL3/SDL.h>

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
                    state, "Opening file", absoluteFilePath);
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
        setMainWindowTitle(
            state,
            state->getActiveDocumentSession().currentFile.empty()
                ? kUntitledDocumentTitle
                : state->getActiveDocumentSession().currentFile);
        return false;
    }
} // namespace cupuacu::actions
