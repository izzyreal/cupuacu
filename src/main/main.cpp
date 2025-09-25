#define SDL_MAIN_USE_CALLBACKS

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "CupuacuState.h"
#include "EventHandling.h"
#include "Gui.h"

const uint16_t initialDimensions[] = { 1280, 420 };

#include <cstdint>
#include <string>
#include <functional>
#include <filesystem>

#include "file_loading.h"

#include "gui/MenuBar.h"

void renderCanvasToWindow(CupuacuState *state)
{
    SDL_SetRenderTarget(state->renderer, NULL);
    SDL_RenderTexture(state->renderer, state->canvas, NULL, NULL);
    SDL_RenderPresent(state->renderer);
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
    CupuacuState *state = new CupuacuState();

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

    if (std::filesystem::exists(state->currentFile))
    {
        loadSampleData(state);
        SDL_SetWindowTitle(state->window, state->currentFile.c_str());
    }
    else
    {
        state->currentFile = "";
    }

    buildComponents(state);
    state->hideSubMenus = [state](){ dynamic_cast<MenuBar*>(state->menuBarHandle)->hideSubMenus(); state->rootComponent->setDirtyRecursive(); };

    resetZoom(state);

    SDL_RenderPresent(state->renderer);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    CupuacuState *state = (CupuacuState*) appstate;

    state->rootComponent->timerCallback();

    SDL_SetRenderTarget(state->renderer, state->canvas);

    const bool somethingIsDirty = state->rootComponent->isDirtyRecursive();

    if (somethingIsDirty)
    {
        state->rootComponent->draw(state->renderer);
        renderCanvasToWindow(state);
    }

    SDL_Delay(16);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    return HandleAppEvent((CupuacuState*)appstate, event);
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    TTF_Quit();
    CupuacuState *state = (CupuacuState*)appstate;
    SDL_DestroyRenderer(state->renderer);
    SDL_DestroyWindow(state->window);
    SDL_Quit();
}

