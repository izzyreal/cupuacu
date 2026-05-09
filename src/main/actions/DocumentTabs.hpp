#pragma once

#include "../DocumentTab.hpp"
#include "../State.hpp"
#include "../gui/MainViewAccess.hpp"
#include "../gui/Waveform.hpp"
#include "DocumentDialogs.hpp"
#include "DocumentUi.hpp"
#include "io/BackgroundSave.hpp"
#include "Play.hpp"
#include "Save.hpp"

#include <algorithm>
#include <filesystem>
#include <string>

namespace cupuacu::actions
{
    inline bool closeTab(cupuacu::State *state, int index);

    inline bool canSwitchTabs(const cupuacu::State *state)
    {
        return state &&
               (!state->audioDevices ||
                (!state->audioDevices->isPlaying() &&
                 !state->audioDevices->isRecording()));
    }

    inline bool documentTabHasUnsavedChanges(const cupuacu::DocumentTab &tab)
    {
        return documentSessionHasUnsavedChanges(tab.session);
    }

    inline cupuacu::UnsavedChangesChoice confirmCloseUnsavedTab(
        cupuacu::State *state, const cupuacu::DocumentTab &tab)
    {
        const std::string title =
            !tab.session.currentFile.empty()
                ? std::filesystem::path(tab.session.currentFile).filename().string()
                : kUntitledDocumentTitle;
        const std::string dialogTitle = "Unsaved changes";
        const std::string message =
            "Save changes to \"" + title + "\" before closing?";

        if (state && state->unsavedChangesReporter)
        {
            return state->unsavedChangesReporter(dialogTitle, message);
        }
        if (SDL_WasInit(SDL_INIT_VIDEO) == 0)
        {
            return cupuacu::UnsavedChangesChoice::Cancel;
        }

        const SDL_MessageBoxButtonData buttons[] = {
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Save"},
            {0, 2, "Discard"},
            {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel"},
        };
        const SDL_MessageBoxData data{
            .flags = SDL_MESSAGEBOX_WARNING,
            .window = state && state->mainDocumentSessionWindow &&
                              state->mainDocumentSessionWindow->getWindow()
                          ? state->mainDocumentSessionWindow->getWindow()
                                ->getSdlWindow()
                          : nullptr,
            .title = dialogTitle.c_str(),
            .message = message.c_str(),
            .numbuttons = 3,
            .buttons = buttons,
            .colorScheme = nullptr,
        };

        int pressedButtonId = 0;
        if (!SDL_ShowMessageBox(&data, &pressedButtonId))
        {
            return cupuacu::UnsavedChangesChoice::Cancel;
        }

        return static_cast<cupuacu::UnsavedChangesChoice>(pressedButtonId);
    }

    inline std::string documentTabTitle(const cupuacu::DocumentTab &tab)
    {
        const std::string baseTitle =
            !tab.session.currentFile.empty()
                ? std::filesystem::path(tab.session.currentFile).filename().string()
                : kUntitledDocumentTitle;

        if (documentTabHasUnsavedChanges(tab))
        {
            return baseTitle + "*";
        }

        return baseTitle;
    }

    inline void refreshActiveTabUi(cupuacu::State *state)
    {
        if (!state)
        {
            return;
        }

        if (state->mainDocumentSessionWindow)
        {
            state->mainDocumentSessionWindow->bindDocumentSession(
                &state->getActiveDocumentSession(), &state->getActiveViewState());
        }

        gui::rebuildMainWaveforms(state);
        gui::Waveform::updateAllSamplePoints(state);
        gui::Waveform::setAllWaveformsDirty(state);
        gui::requestMainViewRefresh(state);
        if (state->mainDocumentSessionWindow)
        {
            state->mainDocumentSessionWindow->getWindow()->refreshForScaleOrResize();
        }
        setMainWindowTitle(state, documentTabTitle(*state->getActiveTab()));
    }

    inline int getTabCount(const cupuacu::State *state)
    {
        return state ? static_cast<int>(state->tabs.size()) : 0;
    }

    inline bool switchToTab(cupuacu::State *state, const int index)
    {
        if (!state || state->tabs.empty())
        {
            return false;
        }
        if (!canSwitchTabs(state))
        {
            return false;
        }
        if (index < 0 || index >= static_cast<int>(state->tabs.size()) ||
            index == state->activeTabIndex)
        {
            return false;
        }

        state->activeTabIndex = index;
        resetSampleValueUnderMouseCursor(state);
        refreshActiveTabUi(state);
        return true;
    }

    inline bool switchToNextTab(cupuacu::State *state)
    {
        if (!state || state->tabs.empty())
        {
            return false;
        }
        if (!canSwitchTabs(state))
        {
            return false;
        }

        const int tabCount = static_cast<int>(state->tabs.size());
        if (tabCount <= 1)
        {
            return false;
        }

        const int nextIndex = (state->activeTabIndex + 1) % tabCount;
        return switchToTab(state, nextIndex);
    }

    inline bool switchToPreviousTab(cupuacu::State *state)
    {
        if (!state || state->tabs.empty())
        {
            return false;
        }
        if (!canSwitchTabs(state))
        {
            return false;
        }

        const int tabCount = static_cast<int>(state->tabs.size());
        if (tabCount <= 1)
        {
            return false;
        }

        const int previousIndex =
            (state->activeTabIndex + tabCount - 1) % tabCount;
        return switchToTab(state, previousIndex);
    }

    inline void appendEmptyTab(cupuacu::State *state)
    {
        if (!state)
        {
            return;
        }
        state->tabs.emplace_back();
        state->tabs.back().title = kUntitledDocumentTitle;
    }

    inline bool createEmptyTab(cupuacu::State *state)
    {
        if (!state)
        {
            return false;
        }
        if (!canSwitchTabs(state))
        {
            return false;
        }

        appendEmptyTab(state);
        state->activeTabIndex = static_cast<int>(state->tabs.size()) - 1;
        closeCurrentDocument(state);
        refreshActiveTabUi(state);
        return true;
    }

    inline bool closeActiveTab(cupuacu::State *state)
    {
        if (!state || state->tabs.empty())
        {
            return false;
        }
        if (!canSwitchTabs(state))
        {
            return false;
        }

        const int removeIndex = state->activeTabIndex;
        return closeTab(state, removeIndex);
    }

    inline bool closeTabWithoutConfirmation(cupuacu::State *state,
                                            const int index)
    {
        if (!state || state->tabs.empty())
        {
            return false;
        }
        if (index < 0 || index >= static_cast<int>(state->tabs.size()))
        {
            return false;
        }

        if (state->tabs.size() == 1)
        {
            closeCurrentDocument(state);
            refreshActiveTabUi(state);
            return true;
        }

        const bool removingActiveTab = index == state->activeTabIndex;
        detail::discardUndoStore(
            state->tabs[static_cast<std::size_t>(index)].session);
        state->tabs.erase(state->tabs.begin() + index);

        if (removingActiveTab)
        {
            if (state->activeTabIndex >= static_cast<int>(state->tabs.size()))
            {
                state->activeTabIndex =
                    static_cast<int>(state->tabs.size()) - 1;
            }
        }
        else if (index < state->activeTabIndex)
        {
            --state->activeTabIndex;
        }

        state->activeTabIndex = std::clamp(
            state->activeTabIndex, 0, static_cast<int>(state->tabs.size()) - 1);
        resetSampleValueUnderMouseCursor(state);
        refreshActiveTabUi(state);
        return true;
    }

    inline bool closeTab(cupuacu::State *state, const int index)
    {
        if (!state || state->tabs.empty())
        {
            return false;
        }
        if (!canSwitchTabs(state))
        {
            return false;
        }
        if (index < 0 || index >= static_cast<int>(state->tabs.size()))
        {
            return false;
        }

        auto &tab = state->tabs[static_cast<std::size_t>(index)];
        if (!documentTabHasUnsavedChanges(tab))
        {
            return closeTabWithoutConfirmation(state, index);
        }

        switch (confirmCloseUnsavedTab(state, tab))
        {
            case cupuacu::UnsavedChangesChoice::Discard:
                state->pendingCloseTabAfterSaveId.reset();
                return closeTabWithoutConfirmation(state, index);
            case cupuacu::UnsavedChangesChoice::Save:
                state->activeTabIndex = index;
                refreshActiveTabUi(state);
                if (tab.session.currentFile.empty())
                {
                    state->pendingCloseTabAfterSaveId = tab.id;
                    showExportAudioDialog(state);
                    return true;
                }

                state->pendingCloseTabAfterSaveId = tab.id;
                if (!io::queueOverwrite(state))
                {
                    state->pendingCloseTabAfterSaveId.reset();
                    return false;
                }
                return true;
            case cupuacu::UnsavedChangesChoice::Cancel:
            default:
                state->pendingCloseTabAfterSaveId.reset();
                return false;
        }
    }
} // namespace cupuacu::actions
