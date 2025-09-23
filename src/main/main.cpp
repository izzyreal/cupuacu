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
#include <functional>
#include <filesystem>

#include "file_loading.h"

#include "gui/Component.h"
#include "gui/OpaqueRect.h"
#include "gui/Waveform.h"
#include "gui/MenuBar.h"
#include "gui/StatusBar.h"
#include "gui/WaveformsOverlay.h"

std::unique_ptr<Component> rootComponent;
Component *backgroundComponentHandle;
Component *menuBarHandle;
Component *statusBarHandle; // Added handle for StatusBar

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
           canvasHeight - (menuHeight*2)
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

SDL_Rect getStatusBarRect(const uint16_t canvasWidth,
                        const uint16_t canvasHeight,
                        const uint8_t hardwarePixelsPerAppPixel,
                        const uint8_t menuFontSize)
{
    const auto statusBarHeight = static_cast<int>((menuFontSize * 1.33) / hardwarePixelsPerAppPixel);

    SDL_Rect result {
        3,
        canvasHeight - statusBarHeight,
        canvasWidth,
        statusBarHeight
    };
    return result;
}

void rebuildComponentTree(CupuacuState *state, bool initializeComponents = false)
{
    const SDL_Point newCanvasDimensions = computeDesiredCanvasDimensions(state->hardwarePixelsPerAppPixel);
    createCanvas(newCanvasDimensions);

    SDL_FPoint actualCanvasDimensions;
    SDL_GetTextureSize(canvas, &actualCanvasDimensions.x, &actualCanvasDimensions.y);

    if (initializeComponents)
    {
        rootComponent = std::make_unique<Component>(state, "RootComponent");
        auto backgroundComponent = std::make_unique<OpaqueRect>(state);
        backgroundComponentHandle = rootComponent->addChildAndSetDirty(backgroundComponent);
    }

    rootComponent->setSize(actualCanvasDimensions.x, actualCanvasDimensions.y);
    backgroundComponentHandle->setSize(actualCanvasDimensions.x, actualCanvasDimensions.y);

    const SDL_Rect menuBarRect = getMenuBarRect(
        actualCanvasDimensions.x,
        actualCanvasDimensions.y,
        state->hardwarePixelsPerAppPixel,
        state->menuFontSize);

    const SDL_Rect waveformRect = getWaveformRect(
        actualCanvasDimensions.x,
        actualCanvasDimensions.y,
        state->hardwarePixelsPerAppPixel,
        menuBarRect.h);

    const SDL_Rect statusBarRect = getStatusBarRect(
        actualCanvasDimensions.x,
        actualCanvasDimensions.y,
        state->hardwarePixelsPerAppPixel,
        state->menuFontSize);

    if (initializeComponents)
    {
        auto waveformsOverlay = std::make_unique<WaveformsOverlay>(state);
        waveformsOverlay->setBounds(
            waveformRect.x,
            waveformRect.y,
            waveformRect.w,
            waveformRect.h
        );
        rootComponent->addChildAndSetDirty(waveformsOverlay);

        state->waveforms.clear();
        int numChannels = static_cast<int>(state->document.channels.size());

        if (numChannels > 0)
        {
            float availableHeight = waveformRect.h;
            float channelHeight = availableHeight / numChannels;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto waveform = std::make_unique<Waveform>(state, ch);
                waveform->setBounds(
                    waveformRect.x,
                    waveformRect.y + ch * channelHeight,
                    waveformRect.w,
                    channelHeight
                );
                auto *handle = rootComponent->addChildAndSetDirty(waveform);
                state->waveforms.push_back(static_cast<Waveform*>(handle));
            }
        }

        auto menuBar = std::make_unique<MenuBar>(state);
        menuBarHandle = rootComponent->addChildAndSetDirty(menuBar);

        auto statusBar = std::make_unique<StatusBar>(state);
        statusBarHandle = rootComponent->addChildAndSetDirty(statusBar);
    }

    menuBarHandle->setBounds(menuBarRect.x, menuBarRect.y, menuBarRect.w, menuBarRect.h);

    const auto samplesPerPixelFactor = Waveform::getWaveformWidth(state) * state->samplesPerPixel;
    const auto newSamplesPerPixel = samplesPerPixelFactor / waveformRect.w;
    state->samplesPerPixel = newSamplesPerPixel;

    const int numChannels = static_cast<int>(state->waveforms.size());
    const float channelHeight = numChannels > 0 ? waveformRect.h / numChannels : 0;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        state->waveforms[ch]->setBounds(
            waveformRect.x,
            waveformRect.y + ch * channelHeight,
            waveformRect.w,
            channelHeight
        );
    }

    if (statusBarHandle)
    {
        statusBarHandle->setBounds(statusBarRect.x, statusBarRect.y, statusBarRect.w, statusBarRect.h);
    }

    rootComponent->setDirtyRecursive();
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
    CupuacuState *state = new CupuacuState();

    resetWaveformState(state);

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

    state->window = window;

    if (std::filesystem::exists(state->currentFile))
    {
        loadSampleData(state);
        SDL_SetWindowTitle(window, state->currentFile.c_str());
    }
    else
    {
        state->currentFile = "";
    }

    rebuildComponentTree(state, true);
    state->hideSubMenus = [&](){ dynamic_cast<MenuBar*>(menuBarHandle)->hideSubMenus(); rootComponent->setDirtyRecursive(); };

    resetZoom(state);

    SDL_RenderPresent(renderer);

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

                if (static_cast<uint16_t>(currentCanvasDimensions.x) != newCanvasDimensions.x ||
                    static_cast<uint16_t>(currentCanvasDimensions.y) != newCanvasDimensions.y)
                {
                    rebuildComponentTree(state);
                }
                break;
            }
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            {
                // Clear highlight for all Waveform components when mouse leaves the window
                if (state->capturingComponent == nullptr && !state->selection.isActive())
                {
                    for (auto* waveform : state->waveforms)
                    {
                        waveform->clearHighlight();
                    }
                    state->componentUnderMouse = nullptr;
                }
                break;
            }
        case SDL_EVENT_KEY_DOWN:
            handleKeyDown(event, state);
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
                    state->mouseX = e.motion.x;
                    state->mouseY = e.motion.y;
                }
                else
                {
                    e.button.x *= canvasDimensions.x / winDimensions.x;
                    e.button.y *= canvasDimensions.y / winDimensions.y;
                }

                // If a component is capturing (e.g., dragging), send button up/down events to it
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP)
                {
                    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP)
                    {
                        const auto newComponentUnderMouse = rootComponent->findComponentAt(e.motion.x, e.motion.y);

                        if (state->componentUnderMouse != newComponentUnderMouse)
                        {
                            if (state->componentUnderMouse != nullptr)
                            {
                                state->componentUnderMouse->mouseLeave();
                            }

                            if (newComponentUnderMouse != nullptr)
                            {
                                newComponentUnderMouse->mouseEnter();
                            }
                        }

                        state->componentUnderMouse = newComponentUnderMouse;
                    }

                    if (state->capturingComponent != nullptr)
                    {
                        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP)
                        {
                            if (!state->capturingComponent->constainsAbsoluteCoordinate(e.button.x, e.button.y))
                            {
                                state->capturingComponent->mouseLeave();
                            }
                        }

                        state->capturingComponent->handleEvent(e);
                        
                        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP)
                        {
                            state->capturingComponent = nullptr; // Clear after handling
                        }
                        break;
                    }
                }

                // Otherwise, send events to the component under the mouse
                rootComponent->handleEvent(e);

                if (e.type == SDL_EVENT_MOUSE_MOTION && state->capturingComponent == nullptr)
                {
                    const auto newComponentUnderMouse = rootComponent->findComponentAt(e.motion.x, e.motion.y);

                    if (state->componentUnderMouse != newComponentUnderMouse)
                    {
                        if (state->componentUnderMouse != nullptr)
                        {
                            state->componentUnderMouse->mouseLeave();
                        }

                        if (newComponentUnderMouse != nullptr)
                        {
                            newComponentUnderMouse->mouseEnter();
                        }
                    }
                    
                    state->componentUnderMouse = newComponentUnderMouse;
                }
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
