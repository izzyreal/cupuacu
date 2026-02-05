#include "Gui.h"

#include "../State.h"
#include "../actions/Zoom.h"

#include "MenuBar.h"
#include "StatusBar.h"
#include "MainView.h"
#include "VuMeterContainer.h"
#include "Window.h"


SDL_Rect computeMainViewBounds(const uint16_t canvasWidth,
                               const uint16_t canvasHeight,
                               const uint8_t pixelScale,
                               const uint8_t menuHeight)
{
    SDL_Rect result{0, menuHeight, canvasWidth,
                    canvasHeight - (menuHeight * 2)};
    return result;
}

SDL_Rect computeMenuBarBounds(const uint16_t canvasWidth,
                              const uint16_t canvasHeight,
                              const uint8_t pixelScale,
                              const uint8_t menuFontSize)
{
    SDL_Rect result{0, 0, canvasWidth,
                    static_cast<int>((menuFontSize * 1.33) / pixelScale)};
    return result;
}

SDL_Rect computeStatusBarBounds(const uint16_t canvasWidth,
                                const uint16_t canvasHeight,
                                const uint8_t pixelScale,
                                const uint8_t menuFontSize)
{
    const auto statusBarHeight =
        static_cast<int>((menuFontSize * 1.33) / pixelScale);

    SDL_Rect result{0, canvasHeight - statusBarHeight, canvasWidth,
                    statusBarHeight};
    return result;
}

SDL_Rect computeVuMeterContainerBounds(const uint16_t canvasWidth,
                                       const uint16_t canvasHeight,
                                       const uint8_t vuMeterContainerHeight,
                                       const SDL_Rect &statusBarRect)
{
    SDL_Rect result{0, statusBarRect.y - vuMeterContainerHeight, canvasWidth,
                    vuMeterContainerHeight};
    return result;
}

void cupuacu::gui::buildComponents(cupuacu::State *state, Window *window)
{
    if (!window)
    {
        return;
    }

    window->setComponentUnderMouse(nullptr);
    window->setCapturingComponent(nullptr);
    window->setOnResize(
        [state, window]
        {
            resizeComponents(state, window);
        });

    auto rootComponent = std::make_unique<Component>(state, "RootComponent");
    rootComponent->setVisible(true);

    auto mainView = std::make_unique<MainView>(state);
    state->mainView = rootComponent->addChild(mainView);

    auto vuMeterContainer = std::make_unique<VuMeterContainer>(state);
    state->vuMeterContainer = rootComponent->addChild(vuMeterContainer);

    auto statusBar = std::make_unique<StatusBar>(state);
    state->statusBar = rootComponent->addChild(statusBar);

    auto menuBar = std::make_unique<MenuBar>(state);
    window->setMenuBar(rootComponent->addChild(menuBar));

    window->setRootComponent(std::move(rootComponent));
    window->refreshForScaleOrResize();
}

void cupuacu::gui::resizeComponents(cupuacu::State *state, Window *window)
{
    if (!window || !window->getCanvas() || !window->getRootComponent())
    {
        return;
    }

    float currentCanvasW = 0.0f, currentCanvasH = 0.0f;
    SDL_GetTextureSize(window->getCanvas(), &currentCanvasW, &currentCanvasH);

    const int newCanvasW = (int)currentCanvasW;
    const int newCanvasH = (int)currentCanvasH;
    window->getRootComponent()->setSize(newCanvasW, newCanvasH);

    const SDL_Rect menuBarRect = computeMenuBarBounds(
        newCanvasW, newCanvasH, state->pixelScale, state->menuFontSize);

    const SDL_Rect statusBarRect = computeStatusBarBounds(
        newCanvasW, newCanvasH, state->pixelScale, state->menuFontSize);

    const int vuMeterContainerHeight = 80 / state->pixelScale;
    const SDL_Rect vuMeterContainerRect = computeVuMeterContainerBounds(
        newCanvasW, newCanvasH, vuMeterContainerHeight, statusBarRect);

    if (window->getMenuBar())
    {
        window->getMenuBar()->setBounds(menuBarRect.x, menuBarRect.y,
                                        menuBarRect.w, menuBarRect.h);
    }

    const SDL_Rect mainViewBounds{0, menuBarRect.h, newCanvasW,
                                  newCanvasH - menuBarRect.h -
                                      vuMeterContainerRect.h - statusBarRect.h};

    state->mainView->setBounds(mainViewBounds.x, mainViewBounds.y,
                               mainViewBounds.w, mainViewBounds.h);

    state->vuMeterContainer->setBounds(
        vuMeterContainerRect.x, vuMeterContainerRect.y, vuMeterContainerRect.w,
        vuMeterContainerRect.h);

    state->statusBar->setBounds(statusBarRect.x, statusBarRect.y,
                                statusBarRect.w, statusBarRect.h);

    window->getRootComponent()->setDirty();

    if (state->samplesPerPixel == 0)
    {
        cupuacu::actions::resetZoom(state);
    }
}
