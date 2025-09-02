#define SDL_MAIN_USE_CALLBACKS

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "CupuacuState.h"

#include "gui/keyboard_handling.h"

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *canvas = NULL;
static SDL_Texture *textTexture = NULL;

const uint16_t initialDimensions[] = { 1280, 720 };

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

#include "file_loading.h"

static const double INITIAL_SAMPLES_PER_PIXEL = 1;
static const double INITIAL_VERTICAL_ZOOM = 1;
static const int64_t INITIAL_SAMPLE_OFFSET = 0;

#include "gui/Component.h"
#include "gui/OpaqueRect.h"
#include "gui/WaveformComponent.h"
#include "gui/MenuBar.h"

std::unique_ptr<Component> rootComponent;
Component *backgroundComponentHandle;
WaveformComponent *waveformComponentHandle;
Component *menuBarHandle;

const std::function<void(CupuacuState*)> renderCanvasToWindow = [](CupuacuState *state)
{
    SDL_SetRenderTarget(renderer, NULL);

    SDL_FPoint currentCanvasDimensions;
    SDL_GetTextureSize(canvas, &currentCanvasDimensions.x, &currentCanvasDimensions.y);
    SDL_FRect dstRect;
    dstRect.x = 0;
    dstRect.y = 0;
    dstRect.w = currentCanvasDimensions.x * state->hardwarePixelsPerAppPixel;
    dstRect.h = currentCanvasDimensions.y * state->hardwarePixelsPerAppPixel;

    SDL_RenderTexture(renderer, canvas, NULL, &dstRect);
    SDL_RenderPresent(renderer);
};

SDL_Point computeDesiredCanvasDimensions(const uint8_t hardwarePixelsPerAppPixel)
{
    SDL_Point result;

    if (!SDL_GetWindowSizeInPixels(window, &result.x, &result.y))
    {
        return {0,0};
    }

    result.x = std::floor(result.x / hardwarePixelsPerAppPixel);
    result.y = std::floor(result.y / hardwarePixelsPerAppPixel);

    return result;
}

void createCanvas(const SDL_Point &dimensions)
{
    if (canvas)
    {
        SDL_DestroyTexture(canvas);
    }

    canvas = SDL_CreateTexture(renderer,
                               SDL_PIXELFORMAT_RGBA8888,
                               SDL_TEXTUREACCESS_TARGET,
                               dimensions.x, dimensions.y);
    SDL_SetTextureScaleMode(canvas, SDL_SCALEMODE_NEAREST);
}

SDL_Rect getWaveformRect(const uint16_t canvasWidth,
                         const uint16_t canvasHeight,
                         const uint8_t hardwarePixelsPerAppPixel,
                         const uint8_t menuHeight)
{
   SDL_Rect result {
           0,
           menuHeight,
           canvasWidth,
           canvasHeight - menuHeight
   };
   return result;
}

SDL_Rect getMenuBarRect(const uint16_t canvasWidth,
                        const uint16_t canvasHeight,
                        const uint8_t hardwarePixelsPerAppPixel,
                        const uint8_t menuFontSize)
{
    SDL_Rect result {
        3,
        0,
        canvasWidth,
        static_cast<int>((menuFontSize * 1.33) / hardwarePixelsPerAppPixel)
    };
    return result;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
    CupuacuState *state = new CupuacuState();

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
                &window,
                &renderer)
            )
    {
        SDL_Log("SDL_CreateWindowAndRenderer() failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    loadSampleData(state);

    SDL_SetWindowTitle(window, state->currentFile.c_str()); 
    SDL_RenderPresent(renderer);

    const SDL_Point newCanvasDimensions = computeDesiredCanvasDimensions(state->hardwarePixelsPerAppPixel);

    createCanvas(newCanvasDimensions);

    SDL_FPoint actualCanvasDimensions;
    SDL_GetTextureSize(canvas, &actualCanvasDimensions.x, &actualCanvasDimensions.y);

    rootComponent = std::make_unique<Component>(state, "RootComponent");
    rootComponent->setBounds(
            0, 0,
            actualCanvasDimensions.x,
            actualCanvasDimensions.y);

    auto backgroundComponent = std::make_unique<OpaqueRect>(state);

    backgroundComponent->setBounds(
            rootComponent->getXPos(),
            rootComponent->getYPos(),
            rootComponent->getWidth(),
            rootComponent->getHeight());

    backgroundComponentHandle = rootComponent->addChildAndSetDirty(backgroundComponent);

    const SDL_Rect menuBarRect = getMenuBarRect(actualCanvasDimensions.x, actualCanvasDimensions.y, state->hardwarePixelsPerAppPixel, state->menuFontSize);

    const SDL_Rect waveformRect = getWaveformRect(actualCanvasDimensions.x, actualCanvasDimensions.y, state->hardwarePixelsPerAppPixel, menuBarRect.h);

    state->samplesPerPixel = state->sampleDataL.size() / (double) waveformRect.w;

    auto waveformComponent = std::make_unique<WaveformComponent>(state);
    waveformComponent->setBounds(waveformRect.x, waveformRect.y, waveformRect.w, waveformRect.h);
    waveformComponentHandle = rootComponent->addChildAndSetDirty(waveformComponent);

    auto menuBar = std::make_unique<MenuBar>(state);

    menuBar->setBounds(menuBarRect.x, menuBarRect.y, menuBarRect.w, menuBarRect.h);
    menuBarHandle = rootComponent->addChildAndSetDirty(menuBar);

    state->hideSubMenus = [&](){ dynamic_cast<MenuBar*>(menuBarHandle)->hideSubMenus(); rootComponent->setDirtyRecursive(); };

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    CupuacuState *state = (CupuacuState*) appstate;

    rootComponent->timerCallback();

    SDL_SetRenderTarget(renderer, canvas);

    const bool somethingIsDirty = rootComponent->isDirtyRecursive();

    if (somethingIsDirty)
    {
        rootComponent->draw(renderer);
        renderCanvasToWindow(state);
    }

    SDL_Delay(16);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    CupuacuState *state = (CupuacuState*)appstate;
    switch (event->type)
    {
        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;
        case SDL_EVENT_WINDOW_RESIZED:
            {
                int winW, winH;
                SDL_GetWindowSize(window, &winW, &winH);

                int hpp = state->hardwarePixelsPerAppPixel;

                int newW = (winW / hpp) * hpp;
                int newH = (winH / hpp) * hpp;

                if (newW != winW || newH != winH)
                {
                    SDL_SetWindowSize(window, newW, newH);
                    break;
                }

                SDL_FPoint currentCanvasDimensions;
                SDL_GetTextureSize(canvas, &currentCanvasDimensions.x, &currentCanvasDimensions.y);
                const SDL_Point newCanvasDimensions = computeDesiredCanvasDimensions(state->hardwarePixelsPerAppPixel);
                const auto currentCanvasWidth = static_cast<uint16_t>(currentCanvasDimensions.x);
                const auto currentCanvasHeight = static_cast<uint16_t>(currentCanvasDimensions.y);

                if (currentCanvasWidth != newCanvasDimensions.x ||
                        currentCanvasHeight != newCanvasDimensions.y)
                {
                    createCanvas(newCanvasDimensions);
                    SDL_FPoint actualCanvasDimensions;
                    SDL_GetTextureSize(canvas, &actualCanvasDimensions.x, &actualCanvasDimensions.y);
                    rootComponent->setSize(actualCanvasDimensions.x, actualCanvasDimensions.y);
                    backgroundComponentHandle->setSize(actualCanvasDimensions.x, actualCanvasDimensions.y);
                    
                    const SDL_Rect menuBarRect = getMenuBarRect(
                            actualCanvasDimensions.x,
                            actualCanvasDimensions.y,
                            state->hardwarePixelsPerAppPixel,
                            state->menuFontSize);

                    menuBarHandle->setBounds(menuBarRect.x, menuBarRect.y, menuBarRect.w, menuBarRect.h);

                    const SDL_Rect waveformRect = getWaveformRect(
                            actualCanvasDimensions.x,
                            actualCanvasDimensions.y,
                            state->hardwarePixelsPerAppPixel,
                            menuBarRect.h);
                    
                    const auto samplesPerPixelFactor = waveformComponentHandle->getWidth() * state->samplesPerPixel;

                    waveformComponentHandle->setBounds(waveformRect.x, waveformRect.y, waveformRect.w, waveformRect.h);
                    
                    const auto newSamplesPerPixel = samplesPerPixelFactor / waveformRect.w; 
                    state->samplesPerPixel = newSamplesPerPixel;

                    rootComponent->setDirtyRecursive();
                }
                break;
            }
        case SDL_EVENT_KEY_DOWN:
            handleKeyDown(
                    event,
                    waveformComponentHandle->getWidth(),
                    state,
                    INITIAL_VERTICAL_ZOOM,
                    INITIAL_SAMPLE_OFFSET,
                    waveformComponentHandle);
            break;
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
            {
                SDL_FPoint canvasDimensions;
                SDL_GetTextureSize(canvas, &canvasDimensions.x, &canvasDimensions.y);

                SDL_Point winDimensions;
                SDL_GetWindowSize(window, &winDimensions.x, &winDimensions.y);

                SDL_Event e = *event;
                
                if (e.type == SDL_EVENT_MOUSE_MOTION)
                {
                    e.motion.x *= canvasDimensions.x / winDimensions.x;
                    e.motion.xrel *= canvasDimensions.x / winDimensions.x;
                    e.motion.y *= canvasDimensions.y / winDimensions.y;
                    e.motion.yrel *= (canvasDimensions.y / winDimensions.y);
                }
                else
                {
                    e.button.x *= canvasDimensions.x / winDimensions.x;
                    e.button.y *= canvasDimensions.y / winDimensions.y;
                }

                rootComponent->handleEvent(e);
            }
            break;
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    TTF_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

