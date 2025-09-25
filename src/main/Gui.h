#pragma once

#include "CupuacuState.h"
#include "gui/MenuBar.h"
#include "gui/StatusBar.h"
#include "gui/WaveformsOverlay.h"
#include "gui/OpaqueRect.h"

static SDL_Point computeRequiredCanvasDimensions(SDL_Window *window, const uint8_t pixelScale)
{
    SDL_Point result;

    if (!SDL_GetWindowSizeInPixels(window, &result.x, &result.y))
    {
        return {0,0};
    }

    result.x = std::floor(result.x / pixelScale);
    result.y = std::floor(result.y / pixelScale);

    return result;
}

static void createCanvas(CupuacuState *state, const SDL_Point &dimensions)
{
    if (state->canvas)
    {
        SDL_DestroyTexture(state->canvas);
    }

    state->canvas = SDL_CreateTexture(state->renderer,
                               SDL_PIXELFORMAT_RGBA8888,
                               SDL_TEXTUREACCESS_TARGET,
                               dimensions.x, dimensions.y);
    SDL_SetTextureScaleMode(state->canvas, SDL_SCALEMODE_NEAREST);
}

static SDL_Rect getWaveformRect(const uint16_t canvasWidth,
                         const uint16_t canvasHeight,
                         const uint8_t pixelScale,
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

static SDL_Rect getMenuBarRect(const uint16_t canvasWidth,
                        const uint16_t canvasHeight,
                        const uint8_t pixelScale,
                        const uint8_t menuFontSize)
{
    SDL_Rect result {
        3,
        0,
        canvasWidth,
        static_cast<int>((menuFontSize * 1.33) / pixelScale)
    };
    return result;
}

static SDL_Rect getStatusBarRect(const uint16_t canvasWidth,
                        const uint16_t canvasHeight,
                        const uint8_t pixelScale,
                        const uint8_t menuFontSize)
{
    const auto statusBarHeight = static_cast<int>((menuFontSize * 1.33) / pixelScale);

    SDL_Rect result {
        3,
        canvasHeight - statusBarHeight,
        canvasWidth,
        statusBarHeight
    };
    return result;
}


static void rebuildComponentTree(CupuacuState *state, bool initializeComponents = false)
{
    float currentCanvasW, currentCanvasH;
    SDL_GetTextureSize(state->canvas, &currentCanvasW, &currentCanvasH);

    const SDL_Point requiredCanvasDimensions = computeRequiredCanvasDimensions(state->window, state->pixelScale);

    if (requiredCanvasDimensions.x == (int) currentCanvasW &&
        requiredCanvasDimensions.y == (int) currentCanvasH)
    {
        return;
    }

    createCanvas(state, requiredCanvasDimensions);

    const int newCanvasW = requiredCanvasDimensions.x, newCanvasH = requiredCanvasDimensions.y;

    if (initializeComponents)
    {
        state->rootComponent = std::make_unique<Component>(state, "RootComponent");
        auto backgroundComponent = std::make_unique<OpaqueRect>(state);
        state->backgroundComponentHandle = state->rootComponent->addChildAndSetDirty(backgroundComponent);
    }

    state->rootComponent->setSize(newCanvasW, newCanvasH);
    state->backgroundComponentHandle->setSize(newCanvasW, newCanvasH);

    const SDL_Rect menuBarRect = getMenuBarRect(
        newCanvasW,
        newCanvasH,
        state->pixelScale,
        state->menuFontSize);

    const SDL_Rect waveformRect = getWaveformRect(
        newCanvasW,
        newCanvasH,
        state->pixelScale,
        menuBarRect.h);

    const SDL_Rect statusBarRect = getStatusBarRect(
        newCanvasW,
        newCanvasH,
        state->pixelScale,
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

        state->waveformsOverlayHandle = state->rootComponent->addChildAndSetDirty(waveformsOverlay);

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
                auto *handle = state->rootComponent->addChildAndSetDirty(waveform);
                state->waveforms.push_back(static_cast<Waveform*>(handle));
            }
        }

        auto menuBar = std::make_unique<MenuBar>(state);
        state->menuBarHandle = state->rootComponent->addChildAndSetDirty(menuBar);

        auto statusBar = std::make_unique<StatusBar>(state);
        state->statusBarHandle = state->rootComponent->addChildAndSetDirty(statusBar);
    }

    state->menuBarHandle->setBounds(menuBarRect.x, menuBarRect.y, menuBarRect.w, menuBarRect.h);

    const auto samplesPerPixelFactor = Waveform::getWaveformWidth(state) * state->samplesPerPixel;
    const auto newSamplesPerPixel = samplesPerPixelFactor / waveformRect.w;
    state->samplesPerPixel = newSamplesPerPixel;

    const int numChannels = static_cast<int>(state->waveforms.size());
    const float channelHeight = numChannels > 0 ? waveformRect.h / numChannels : 0;

    state->waveformsOverlayHandle->setBounds(
        waveformRect.x,
        waveformRect.y,
        waveformRect.w,
        waveformRect.h
    );

    for (int ch = 0; ch < numChannels; ++ch)
    {
        state->waveforms[ch]->setBounds(
            waveformRect.x,
            waveformRect.y + ch * channelHeight,
            waveformRect.w,
            channelHeight
        );
    }

    state->statusBarHandle->setBounds(statusBarRect.x, statusBarRect.y, statusBarRect.w, statusBarRect.h);

    state->rootComponent->setDirtyRecursive();
}

