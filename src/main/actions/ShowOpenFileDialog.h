#pragma once
#include <SDL3/SDL.h>

#include "../State.h"
#include "../file/file_loading.h"
#include "../gui/MainView.h"
#include "../gui/Gui.h"
#include "../gui/Waveform.h"
#include "Zoom.h"

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

        std::string absoluteFilePath;

        while (*filelist)
        {
            // SDL_Log("Full path to selected file: '%s'", *filelist);
            absoluteFilePath = *filelist;
            break;
            filelist++;
        }

        auto state = (cupuacu::State *)userdata;

        state->currentFile = absoluteFilePath;

        file::loadSampleData(state);
        state->mainView->rebuildWaveforms();
        resetWaveformState(state);
        resetZoom(state);
        gui::Waveform::updateAllSamplePoints(state);
        gui::Waveform::setAllWaveformsDirty(state);

        SDL_SetWindowTitle(state->window, state->currentFile.c_str());
    }

    static void ShowDialogMainThreadCallback(void *userdata)
    {
        SDL_ShowOpenFileDialog(fileDialogCallback, (State *)userdata, NULL,
                               filters, 1, NULL, false);
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
