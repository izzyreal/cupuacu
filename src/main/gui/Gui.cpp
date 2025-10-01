#include "Gui.h"

#include "../CupuacuState.h"

#include "MenuBar.h"
#include "StatusBar.h"
#include "MainView.h"
#include "OpaqueRect.h"

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

SDL_Rect getMenuBarRect(const uint16_t canvasWidth,
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

SDL_Rect getStatusBarRect(const uint16_t canvasWidth,
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

void buildComponents(CupuacuState *state)
{
    state->componentUnderMouse = nullptr;
    state->capturingComponent = nullptr;
    state->rootComponent = std::make_unique<Component>(state, "RootComponent");
    state->rootComponent->setVisible(true);

    auto backgroundComponent = std::make_unique<OpaqueRect>(state);
    state->backgroundComponent = state->rootComponent->addChild(backgroundComponent);

    auto mainView = std::make_unique<MainView>(state);
    state->mainView = state->rootComponent->addChild(mainView);

    auto menuBar = std::make_unique<MenuBar>(state);
    state->menuBar = state->rootComponent->addChild(menuBar);

    auto statusBar = std::make_unique<StatusBar>(state);
    state->statusBar = state->rootComponent->addChild(statusBar);

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
    state->backgroundComponent->setSize(newCanvasW, newCanvasH);

    const SDL_Rect menuBarRect = getMenuBarRect(
        newCanvasW,
        newCanvasH,
        state->pixelScale,
        state->menuFontSize);

    const SDL_Rect statusBarRect = getStatusBarRect(
        newCanvasW,
        newCanvasH,
        state->pixelScale,
        state->menuFontSize);

    state->menuBar->setBounds(menuBarRect.x, menuBarRect.y, menuBarRect.w, menuBarRect.h);

    const SDL_Rect mainViewBounds = computeMainViewBounds(
        newCanvasW,
        newCanvasH,
        state->pixelScale,
        menuBarRect.h);

    state->mainView->setBounds(
        mainViewBounds.x,
        mainViewBounds.y,
        mainViewBounds.w,
        mainViewBounds.h
    );

    state->statusBar->setBounds(statusBarRect.x, statusBarRect.y, statusBarRect.w, statusBarRect.h);

    state->rootComponent->setDirty();
}

