#define SDL_MAIN_USE_CALLBACKS

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "CupuacuState.h"
#include "gui/EventHandling.h"
#include "gui/Gui.h"

const uint16_t initialDimensions[] = { 1280, 720 };

#include <cstdint>
#include <string>
#include <filesystem>

#include "file_loading.h"

#include "gui/MenuBar.h"

void renderCanvasToWindow3(CupuacuState *state)
{
    SDL_SetRenderTarget(state->renderer, NULL);
    SDL_RenderTexture(state->renderer, state->canvas, NULL, NULL);
    SDL_RenderPresent(state->renderer);
}

void renderCanvasToWindow(CupuacuState *state)
{
    if (state->dirtyRects.empty()) return;

    SDL_SetRenderTarget(state->renderer, NULL);

    SDL_FRect mergedRect = state->dirtyRects[0];
    for (size_t i = 1; i < state->dirtyRects.size(); ++i)
    {
        SDL_GetRectUnionFloat(&mergedRect, &state->dirtyRects[i], &mergedRect);
    }

    float canvasW, canvasH;
    SDL_GetTextureSize(state->canvas, &canvasW, &canvasH);

    int windowW, windowH;
    SDL_GetCurrentRenderOutputSize(state->renderer, &windowW, &windowH);

    float scaleX = static_cast<float>(windowW) / canvasW;
    float scaleY = static_cast<float>(windowH) / canvasH;

    SDL_FRect srcRect = mergedRect;
    SDL_FRect dstRect {
        mergedRect.x * scaleX,
        mergedRect.y * scaleY,
        mergedRect.w * scaleX,
        mergedRect.h * scaleY
    };

    SDL_RenderTexture(state->renderer, state->canvas, &srcRect, &dstRect);
    SDL_RenderPresent(state->renderer);

    state->dirtyRects.clear();
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

    initCursors();

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

    resetZoom(state);

    SDL_RenderPresent(state->renderer);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    CupuacuState *state = (CupuacuState*) appstate;

    state->rootComponent->timerCallbackRecursive();

    SDL_SetRenderTarget(state->renderer, state->canvas);
    state->rootComponent->draw(state->renderer);
    renderCanvasToWindow3(state);
    state->dirtyRects.clear();

    SDL_Delay(16);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    return handleAppEvent((CupuacuState*)appstate, event);
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    TTF_Quit();
    CupuacuState *state = (CupuacuState*)appstate;
    SDL_DestroyRenderer(state->renderer);
    SDL_DestroyWindow(state->window);
    SDL_Quit();
}

