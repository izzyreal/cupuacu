#include "Gui.hpp"

#include "../State.hpp"
#include "../actions/Zoom.hpp"

#include "MenuBar.hpp"
#include "StatusBar.hpp"
#include "MainView.hpp"
#include "TransportButtonsContainer.hpp"
#include "VuMeterContainer.hpp"
#include "Window.hpp"

SDL_Rect computeMainViewBounds(const uint16_t canvasWidth,
                               const uint16_t canvasHeight,
                               const uint8_t pixelScale,
                               const uint8_t menuHeight)
{
    const SDL_Rect result{0, menuHeight, canvasWidth,
                          canvasHeight - menuHeight * 2};
    return result;
}

SDL_Rect computeMenuBarBounds(const uint16_t canvasWidth,
                              const uint16_t canvasHeight,
                              const uint8_t pixelScale,
                              const uint8_t menuFontSize)
{
    const SDL_Rect result{0, 0, canvasWidth,
                          static_cast<int>(menuFontSize * 1.33 / pixelScale)};
    return result;
}

SDL_Rect computeStatusBarBounds(const uint16_t canvasWidth,
                                const uint16_t canvasHeight,
                                const uint8_t pixelScale,
                                const uint8_t menuFontSize)
{
    const auto statusBarHeight =
        static_cast<int>(menuFontSize * 1.33 / pixelScale);

    const SDL_Rect result{0, canvasHeight - statusBarHeight, canvasWidth,
                          statusBarHeight};
    return result;
}

SDL_Rect computeVuMeterContainerBounds(const uint16_t canvasWidth,
                                       const uint16_t canvasHeight,
                                       const uint8_t vuMeterContainerHeight,
                                       const SDL_Rect &statusBarRect)
{
    const int transportWidth = canvasWidth / 3;
    const SDL_Rect result{transportWidth,
                          statusBarRect.y - vuMeterContainerHeight,
                          canvasWidth - transportWidth, vuMeterContainerHeight};
    return result;
}

SDL_Rect computeTransportButtonsContainerBounds(
    const uint16_t canvasWidth, const uint16_t canvasHeight,
    const uint8_t vuMeterContainerHeight, const SDL_Rect &statusBarRect)
{
    const int transportWidth = canvasWidth / 3;
    const SDL_Rect result{0, statusBarRect.y - vuMeterContainerHeight,
                          transportWidth, vuMeterContainerHeight};
    return result;
}

void cupuacu::gui::buildComponents(State *state, Window *window)
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

    auto contentLayer = std::make_unique<Component>(state, "ContentLayer");
    contentLayer->setInterceptMouseEnabled(false);
    auto *contentLayerPtr = rootComponent->addChild(contentLayer);

    auto overlayLayer = std::make_unique<Component>(state, "OverlayLayer");
    overlayLayer->setInterceptMouseEnabled(false);
    auto *overlayLayerPtr = rootComponent->addChild(overlayLayer);

    auto mainView = std::make_unique<MainView>(state);
    state->mainView = contentLayerPtr->addChild(mainView);

    auto vuMeterContainer = std::make_unique<VuMeterContainer>(state);
    state->vuMeterContainer = contentLayerPtr->addChild(vuMeterContainer);

    auto transportButtonsContainer =
        std::make_unique<TransportButtonsContainer>(state);
    state->transportButtonsContainer =
        contentLayerPtr->addChild(transportButtonsContainer);

    auto statusBar = std::make_unique<StatusBar>(state);
    state->statusBar = contentLayerPtr->addChild(statusBar);

    auto menuBar = std::make_unique<MenuBar>(state);
    auto *menuBarPtr = overlayLayerPtr->addChild(menuBar);

    window->setRootComponent(std::move(rootComponent));
    window->setContentLayer(contentLayerPtr);
    window->setOverlayLayer(overlayLayerPtr);
    window->setMenuBar(menuBarPtr);
    window->refreshForScaleOrResize();
}

void cupuacu::gui::resizeComponents(State *state, Window *window)
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
    if (window->getContentLayer())
    {
        window->getContentLayer()->setBounds(0, 0, newCanvasW, newCanvasH);
    }
    if (window->getOverlayLayer())
    {
        window->getOverlayLayer()->setBounds(0, 0, newCanvasW, newCanvasH);
    }

    const SDL_Rect menuBarRect = computeMenuBarBounds(
        newCanvasW, newCanvasH, state->pixelScale, state->menuFontSize);

    const SDL_Rect statusBarRect = computeStatusBarBounds(
        newCanvasW, newCanvasH, state->pixelScale, state->menuFontSize);

    const int vuMeterContainerHeight = 80 / state->pixelScale;
    const SDL_Rect transportButtonsContainerRect =
        computeTransportButtonsContainerBounds(
            newCanvasW, newCanvasH, vuMeterContainerHeight, statusBarRect);
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
    state->transportButtonsContainer->setBounds(
        transportButtonsContainerRect.x, transportButtonsContainerRect.y,
        transportButtonsContainerRect.w, transportButtonsContainerRect.h);

    state->statusBar->setBounds(statusBarRect.x, statusBarRect.y,
                                statusBarRect.w, statusBarRect.h);

    window->getRootComponent()->setDirty();

    auto &viewState = state->mainDocumentSessionWindow->getViewState();
    if (viewState.samplesPerPixel == 0)
    {
        actions::resetZoom(state);
    }
}
