#define SDL_MAIN_USE_CALLBACKS

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "State.h"
#include "gui/EventHandling.h"
#include "gui/Gui.h"

const uint16_t initialDimensions[] = { 1280, 720 };

#include <cstdint>
#include <string>
#include <filesystem>

#include "file/file_loading.h"

void renderCanvasToWindow(cupuacu::State *state)
{
    if (state->dirtyRects.empty()) return;

    SDL_SetRenderTarget(state->renderer, NULL);

    SDL_RenderTexture(state->renderer, state->canvas, NULL, NULL);
    SDL_RenderPresent(state->renderer);
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
    cupuacu::State *state = new cupuacu::State();

    resetWaveformState(state);
    resetSampleValueUnderMouseCursor(state);

    *appstate = state;

    SDL_SetAppMetadata("Cupuacu -- A minimalist audio editor by Izmar", "0.1", "nl.izmar.cupuacu");

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("SDL_Init(SDL_INIT_VIDEO) failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!TTF_Init())
    {
        SDL_Log("TTF_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_CreateWindowAndRenderer(
                "",
                initialDimensions[0],
                initialDimensions[1],
                SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                &state->window,
                &state->renderer)
            )
    {
        SDL_Log("SDL_CreateWindowAndRenderer() failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    cupuacu::gui::initCursors();

    if (std::filesystem::exists(state->currentFile))
    {
        cupuacu::file::loadSampleData(state);
        SDL_SetWindowTitle(state->window, state->currentFile.c_str());
    }
    else
    {
        state->currentFile = "";
    }

    cupuacu::gui::buildComponents(state);

    cupuacu::actions::resetZoom(state);

    SDL_RenderPresent(state->renderer);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    cupuacu::State *state = (cupuacu::State*) appstate;

    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 frameStart = SDL_GetPerformanceCounter();

    state->rootComponent->timerCallbackRecursive();
    SDL_SetRenderTarget(state->renderer, state->canvas);
    state->rootComponent->draw(state->renderer);
    renderCanvasToWindow(state);
    state->dirtyRects.clear();

    // Compute elapsed time of work
    Uint64 workEnd = SDL_GetPerformanceCounter();
    Uint64 workNS = (workEnd - frameStart) * 1'000'000'000 / freq;

    // Target 16ms = 16,000,000 ns
    const Uint64 targetNS = 16'000'000 - 931'000;
    if (workNS < targetNS)
    {
        SDL_DelayNS(targetNS - workNS);
    }

    // Total frame time including delay
    Uint64 frameEnd = SDL_GetPerformanceCounter();
    Uint64 totalNS = (frameEnd - frameStart) * 1'000'000'000 / freq;
    double totalMS = totalNS / 1'000'000.0;
    double deviationMS = totalMS - 16.0;

    //printf("Frame total: %.5f ms, deviation from 16ms: %+.5f ms\n", totalMS, deviationMS);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    return cupuacu::gui::handleAppEvent((cupuacu::State*)appstate, event);
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    TTF_Quit();
    cupuacu::State *state = (cupuacu::State*)appstate;
    SDL_DestroyRenderer(state->renderer);
    SDL_DestroyWindow(state->window);
    SDL_Quit();
}

