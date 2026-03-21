#pragma once
#include <SDL3/SDL.h>

#include "DocumentLifecycle.hpp"

#include "../State.hpp"

#include <string>

namespace cupuacu::actions
{
    const SDL_DialogFileFilter filters[] = {{"WAV audio", "wav"}};

    static auto initialPath = SDL_GetUserFolder(SDL_FOLDER_HOME);

    static void fileDialogCallback(void *userdata, const char *const *filelist,
                                   int filter)
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

        auto *state = (cupuacu::State *)userdata;
        while (*filelist)
        {
            actions::loadFileIntoNewTab(state, *filelist);
            ++filelist;
        }
    }

    static void ShowDialogMainThreadCallback(void *userdata)
    {
        SDL_ShowOpenFileDialog(fileDialogCallback, (State *)userdata, NULL,
                               filters, 1, NULL, true);
    }

    static Uint32 ShowDialogTimerCallback(void *userdata, SDL_TimerID,
                                          Uint32 interval)
    {
        SDL_RunOnMainThread(ShowDialogMainThreadCallback, userdata, false);
        return 0;
    }

    static void showOpenFileDialog(cupuacu::State *state)
    {
        SDL_AddTimer(0, ShowDialogTimerCallback, (void *)state);
    }
} // namespace cupuacu::actions
