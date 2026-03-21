#pragma once
#include <SDL3/SDL.h>

#include "Save.hpp"

#include "../State.hpp"

#include <string>

namespace cupuacu::actions
{
    const SDL_DialogFileFilter saveFilters[] = {{"WAV audio", "wav"}};

    static void saveFileDialogCallback(void *userdata,
                                       const char *const *filelist, int)
    {
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

        auto *state = static_cast<cupuacu::State *>(userdata);
        actions::saveAs(state, *filelist);
    }

    static void showSaveFileDialogMainThreadCallback(void *userdata)
    {
        auto *state = static_cast<cupuacu::State *>(userdata);
        const char *defaultLocation =
            (state && !state->getActiveDocumentSession().currentFile.empty())
                ? state->getActiveDocumentSession().currentFile.c_str()
                : SDL_GetUserFolder(SDL_FOLDER_HOME);
        SDL_ShowSaveFileDialog(saveFileDialogCallback, userdata, nullptr,
                               saveFilters, 1, defaultLocation);
    }

    static Uint32 showSaveFileDialogTimerCallback(void *userdata, SDL_TimerID,
                                                  Uint32)
    {
        SDL_RunOnMainThread(showSaveFileDialogMainThreadCallback, userdata, false);
        return 0;
    }

    static void showSaveFileDialog(cupuacu::State *state)
    {
        SDL_AddTimer(0, showSaveFileDialogTimerCallback, static_cast<void *>(state));
    }
} // namespace cupuacu::actions
