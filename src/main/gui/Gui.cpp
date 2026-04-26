#include "Gui.hpp"

#include "../State.hpp"
#include "../actions/Zoom.hpp"

#include "MenuBar.hpp"
#include "MenuLayoutPlanning.hpp"
#include "StatusBar.hpp"
#include "MainView.hpp"
#include "LongTaskOverlay.hpp"
#include "TabStrip.hpp"
#include "TransportButtonsContainer.hpp"
#include "UiScale.hpp"
#include "VuMeterContainer.hpp"
#include "Window.hpp"

namespace
{
    template <typename T>
    T *findDirectChildOfType(cupuacu::gui::Component *parent)
    {
        if (!parent)
        {
            return nullptr;
        }

        for (const auto &child : parent->getChildren())
        {
            if (auto *typed = dynamic_cast<T *>(child.get()))
            {
                return typed;
            }
        }

        return nullptr;
    }
}

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
                          cupuacu::gui::scaleUi(nullptr, static_cast<float>(menuFontSize) * 1.33f)};
    return result;
}

SDL_Rect computeStatusBarBounds(const uint16_t canvasWidth,
                                const uint16_t canvasHeight,
                                const uint8_t pixelScale,
                                const uint8_t menuFontSize)
{
    const auto statusBarHeight = cupuacu::gui::scaleUi(
        nullptr, static_cast<float>(menuFontSize) * 1.33f);

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
    contentLayerPtr->addChild(mainView);

    auto tabStrip = std::make_unique<TabStrip>(state);
    contentLayerPtr->addChild(tabStrip);

    auto vuMeterContainer = std::make_unique<VuMeterContainer>(state);
    contentLayerPtr->addChild(vuMeterContainer);

    auto transportButtonsContainer =
        std::make_unique<TransportButtonsContainer>(state);
    contentLayerPtr->addChild(transportButtonsContainer);

    auto statusBar = std::make_unique<StatusBar>(state);
    contentLayerPtr->addChild(statusBar);

    auto menuBar = std::make_unique<MenuBar>(state);
    auto *menuBarPtr = overlayLayerPtr->addChild(menuBar);

    auto longTaskOverlay = std::make_unique<LongTaskOverlay>(state);
    overlayLayerPtr->addChild(longTaskOverlay);

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

    const int menuBarHeight = menuItemHeight(state);
    const SDL_Rect menuBarRect{0, 0, newCanvasW, menuBarHeight};

    const SDL_Rect statusBarRect{
        0,
        newCanvasH - menuBarHeight,
        newCanvasW,
        menuBarHeight};

    const int vuMeterContainerHeight = scaleUi(state, 80.0f);
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

    auto *contentLayer = window->getContentLayer();
    auto *mainView = findDirectChildOfType<cupuacu::gui::MainView>(contentLayer);
    auto *tabStrip = findDirectChildOfType<cupuacu::gui::TabStrip>(contentLayer);
    auto *vuMeterContainer =
        findDirectChildOfType<cupuacu::gui::VuMeterContainer>(contentLayer);
    auto *transportButtonsContainer =
        findDirectChildOfType<cupuacu::gui::TransportButtonsContainer>(
            contentLayer);
    auto *statusBar =
        findDirectChildOfType<cupuacu::gui::StatusBar>(contentLayer);
    auto *longTaskOverlay =
        findDirectChildOfType<cupuacu::gui::LongTaskOverlay>(
            window->getOverlayLayer());

    if (!mainView || !tabStrip || !vuMeterContainer || !transportButtonsContainer ||
        !statusBar || !longTaskOverlay)
    {
        return;
    }

    const int tabStripHeight = menuBarHeight;

    const SDL_Rect mainViewBounds{0, menuBarRect.h, newCanvasW,
                                  newCanvasH - menuBarRect.h - tabStripHeight -
                                      vuMeterContainerRect.h - statusBarRect.h};

    tabStrip->setBounds(0, menuBarRect.h, newCanvasW, tabStripHeight);

    mainView->setBounds(mainViewBounds.x, mainViewBounds.y + tabStripHeight,
                        mainViewBounds.w,
                        mainViewBounds.h);

    vuMeterContainer->setBounds(
        vuMeterContainerRect.x, vuMeterContainerRect.y,
        vuMeterContainerRect.w, vuMeterContainerRect.h);
    transportButtonsContainer->setBounds(
        transportButtonsContainerRect.x, transportButtonsContainerRect.y,
        transportButtonsContainerRect.w, transportButtonsContainerRect.h);

    statusBar->setBounds(statusBarRect.x, statusBarRect.y, statusBarRect.w,
                         statusBarRect.h);
    longTaskOverlay->setBounds(0, 0, newCanvasW, newCanvasH);

    window->getRootComponent()->setDirty();

    auto &viewState = state->getActiveViewState();
    if (viewState.samplesPerPixel == 0)
    {
        actions::resetZoom(state);
    }
}
