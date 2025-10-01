#include "Gui.h"

#include "../CupuacuState.h"
#include "../actions/Zoom.h"

#include "MenuBar.h"
#include "StatusBar.h"
#include "MainView.h"
#include "VuMeterContainer.h"

#include <cmath>

SDL_Point computeRequiredCanvasDimensions(SDL_Window *window, const uint8_t pixelScale)
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

void createCanvas(CupuacuState *state, const SDL_Point &dimensions)
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

SDL_Rect computeMainViewBounds(const uint16_t canvasWidth,
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

SDL_Rect computeMenuBarBounds(const uint16_t canvasWidth,
                              const uint16_t canvasHeight,
                              const uint8_t pixelScale,
                              const uint8_t menuFontSize)
{
    SDL_Rect result {
        0,
        0,
        canvasWidth,
        static_cast<int>((menuFontSize * 1.33) / pixelScale)
    };
    return result;
}

SDL_Rect computeStatusBarBounds(const uint16_t canvasWidth,
                                const uint16_t canvasHeight,
                                const uint8_t pixelScale,
                                const uint8_t menuFontSize)
{
    const auto statusBarHeight = static_cast<int>((menuFontSize * 1.33) / pixelScale);

    SDL_Rect result {
        0,
        canvasHeight - statusBarHeight,
        canvasWidth,
        statusBarHeight
    };
    return result;
}

SDL_Rect computeVuMeterContainerBounds(const uint16_t canvasWidth,
                              const uint16_t canvasHeight,
                              const uint8_t vuMeterContainerHeight,
                              const SDL_Rect &statusBarRect)
{
    SDL_Rect result {
        0,
        statusBarRect.y - vuMeterContainerHeight,
        canvasWidth,
        vuMeterContainerHeight
    };
    return result;
}

void buildComponents(CupuacuState *state)
{
    state->componentUnderMouse = nullptr;
    state->capturingComponent = nullptr;
    state->rootComponent = std::make_unique<Component>(state, "RootComponent");
    state->rootComponent->setVisible(true);

    auto mainView = std::make_unique<MainView>(state);
    state->mainView = state->rootComponent->addChild(mainView);

    auto vuMeterContainer = std::make_unique<VuMeterContainer>(state);
    state->vuMeterContainer = state->rootComponent->addChild(vuMeterContainer);

    auto statusBar = std::make_unique<StatusBar>(state);
    state->statusBar = state->rootComponent->addChild(statusBar);

    auto menuBar = std::make_unique<MenuBar>(state);
    state->menuBar = state->rootComponent->addChild(menuBar);

    resizeComponents(state);
}

void resizeComponents(CupuacuState *state)
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
    state->rootComponent->setSize(newCanvasW, newCanvasH);

    const SDL_Rect menuBarRect = computeMenuBarBounds(
        newCanvasW,
        newCanvasH,
        state->pixelScale,
        state->menuFontSize);

    const SDL_Rect statusBarRect = computeStatusBarBounds(
        newCanvasW,
        newCanvasH,
        state->pixelScale,
        state->menuFontSize);

    const int vuMeterContainerHeight = 80 / state->pixelScale;
    const SDL_Rect vuMeterContainerRect = computeVuMeterContainerBounds(newCanvasW, newCanvasH, vuMeterContainerHeight, statusBarRect);

    state->menuBar->setBounds(menuBarRect.x, menuBarRect.y, menuBarRect.w, menuBarRect.h);

    const SDL_Rect mainViewBounds {
        0,
        menuBarRect.h,
        newCanvasW,
        newCanvasH - menuBarRect.h - vuMeterContainerRect.h - statusBarRect.h
    };

    state->mainView->setBounds(
        mainViewBounds.x,
        mainViewBounds.y,
        mainViewBounds.w,
        mainViewBounds.h
    );

    state->vuMeterContainer->setBounds(vuMeterContainerRect.x, vuMeterContainerRect.y, vuMeterContainerRect.w, vuMeterContainerRect.h);

    state->statusBar->setBounds(statusBarRect.x, statusBarRect.y, statusBarRect.w, statusBarRect.h);

    state->rootComponent->setDirty();

    if (state->samplesPerPixel == 0)
    {
        resetZoom(state);
    }
}

