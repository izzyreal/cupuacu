#pragma once
#include <SDL3/SDL.h>

#include "../file/AudioExport.hpp"
#include "Save.hpp"

#include "../State.hpp"

#include <string>

namespace cupuacu::actions
{
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

    static void showSaveFileDialogMainThreadCallback(void *userdata)
    {
        auto *state = static_cast<cupuacu::State *>(userdata);
        if (!state || !state->pendingSaveAsExportSettings.has_value())
        {
            return;
        }

        const char *defaultLocation =
            (state && !state->getActiveDocumentSession().currentFile.empty())
                ? state->getActiveDocumentSession().currentFile.c_str()
                : SDL_GetUserFolder(SDL_FOLDER_HOME);
        SDL_ShowSaveFileDialog(saveFileDialogCallback, userdata, nullptr, nullptr,
                               0, defaultLocation);
    }

    static Uint32 showSaveFileDialogTimerCallback(void *userdata, SDL_TimerID,
                                                  Uint32)
    {
        SDL_RunOnMainThread(showSaveFileDialogMainThreadCallback, userdata, false);
        return 0;
    }

    static void showSaveFileDialog(cupuacu::State *state,
                                   const file::AudioExportSettings &settings)
    {
        if (!state || !settings.isValid())
        {
            return;
        }

        state->pendingSaveAsExportSettings = settings;
        SDL_AddTimer(0, showSaveFileDialogTimerCallback, static_cast<void *>(state));
    }
} // namespace cupuacu::actions
