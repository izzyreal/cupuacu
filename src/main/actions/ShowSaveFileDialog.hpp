#pragma once
#include <SDL3/SDL.h>

#include "../file/AudioExport.hpp"
#include "Save.hpp"

#include "../State.hpp"

#include <string>

namespace cupuacu::actions
{
    static SDL_Window *getSaveFileDialogParentWindow(cupuacu::State *state)
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

    static void saveFileDialogCallback(void *userdata,
                                       const char *const *filelist, int)
    {
        auto *state = static_cast<cupuacu::State *>(userdata);
        const auto settings =
            state ? state->pendingSaveAsExportSettings : std::nullopt;
        if (state)
        {
            state->pendingSaveAsExportSettings.reset();
        }

        if (!filelist)
        {
            SDL_Log("An error occured: %s", SDL_GetError());
            return;
        }
        else if (!*filelist)
        {
            SDL_Log("The user did not select any file.");
            SDL_Log("Most likely, the dialog was canceled.");
            return;
        }

        if (!state || !settings.has_value())
        {
            return;
        }

        actions::saveAs(state, *filelist, *settings);
    }

    static void showSaveFileDialog(cupuacu::State *state,
                                   const file::AudioExportSettings &settings)
    {
        if (!state || !settings.isValid())
        {
            return;
        }

        state->pendingSaveAsExportSettings = settings;
        const char *defaultLocation =
            !state->getActiveDocumentSession().currentFile.empty()
                ? state->getActiveDocumentSession().currentFile.c_str()
                : SDL_GetUserFolder(SDL_FOLDER_HOME);
        SDL_ShowSaveFileDialog(
            saveFileDialogCallback, state, getSaveFileDialogParentWindow(state),
            nullptr, 0, defaultLocation);
    }
} // namespace cupuacu::actions
