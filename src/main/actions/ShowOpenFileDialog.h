#pragma once
#include <SDL3/SDL.h>

#include "../CupuacuState.h"
#include "../file_loading.h"

#include "../gui/WaveformComponent.h"

#include <string>

const SDL_DialogFileFilter filters[] = {
    { "WAV audio", "wav" }
};

static auto initialPath = SDL_GetUserFolder(SDL_FOLDER_HOME);

static void fileDialogCallback(void *userdata, const char * const *filelist, int filter)
{
    if (!filelist) {
        SDL_Log("An error occured: %s", SDL_GetError());
        return;
    } else if (!*filelist) {
        SDL_Log("The user did not select any file.");
        SDL_Log("Most likely, the dialog was canceled.");
        return;
    }

    std::string absoluteFilePath;

    while (*filelist) {
        //SDL_Log("Full path to selected file: '%s'", *filelist);
        absoluteFilePath = *filelist;
        break;
        filelist++;
    }

    auto state = (CupuacuState*)userdata;

    state->currentFile = absoluteFilePath;

    loadSampleData(state);
    const auto waveformWidth = state->waveformComponent->getWidth();
    state->samplesPerPixel = state->sampleDataL.size() / (float) waveformWidth;
    resetWaveformState(state);
    dynamic_cast<WaveformComponent*>(state->waveformComponent)->updateSamplePoints();
    state->waveformComponent->setDirty();

    SDL_SetWindowTitle(state->window, state->currentFile.c_str()); 
}

static void showOpenFileDialog(CupuacuState *state)
{
    SDL_ShowOpenFileDialog(fileDialogCallback, state, NULL, filters, 1, NULL, false);
}

