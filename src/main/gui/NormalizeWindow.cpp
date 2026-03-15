#include "NormalizeWindow.hpp"

#include "../State.hpp"
#include "Colors.hpp"
#include "Component.hpp"
#include "UiScale.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

using namespace cupuacu::gui;

namespace
{
    constexpr int kWindowWidth = 420;
    constexpr int kWindowHeight = 220;

    constexpr Uint32 getHighDensityWindowFlag()
    {
#if defined(__linux__)
        return 0;
#else
        return SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif
    }
} // namespace

NormalizeWindow::NormalizeWindow(State *stateToUse) : state(stateToUse)
{
    if (!state)
    {
        return;
    }

    window = std::make_unique<Window>(
        state, "Normalize", kWindowWidth, kWindowHeight,
        SDL_WINDOW_RESIZABLE | getHighDensityWindowFlag());
    if (!window->isOpen())
    {
        return;
    }

    state->windows.push_back(window.get());

    auto rootComponent = std::make_unique<Component>(state, "NormalizeRoot");
    rootComponent->setVisible(true);

    background =
        rootComponent->emplaceChild<OpaqueRect>(state, Colors::background);
    messageLabel = rootComponent->emplaceChild<Label>(
        state, "Normalize UI scaffold. Parameters can be added here next.");
    cancelButton = rootComponent->emplaceChild<TextButton>(state, "Cancel");
    applyButton = rootComponent->emplaceChild<TextButton>(state, "Apply");

    messageLabel->setFontSize(std::max(16, (int)state->menuFontSize - 6));
    cancelButton->setTriggerOnMouseUp(true);
    applyButton->setTriggerOnMouseUp(true);
    cancelButton->setOnPress([this]() { closeNow(); });
    applyButton->setOnPress([this]() { closeNow(); });

    window->setOnResize([this]() { layoutComponents(); });
    window->setOnClose(
        [this]
        {
            if (window && state)
            {
                const auto it = std::find(state->windows.begin(), state->windows.end(),
                                          window.get());
                if (it != state->windows.end())
                {
                    state->windows.erase(it);
                }
            }

        });

    window->setRootComponent(std::move(rootComponent));
    layoutComponents();
    window->renderFrame();
}

NormalizeWindow::~NormalizeWindow()
{
    if (window && state)
    {
        const auto it = std::find(state->windows.begin(), state->windows.end(),
                                  window.get());
        if (it != state->windows.end())
        {
            state->windows.erase(it);
        }
    }
}

void NormalizeWindow::raise() const
{
    if (window && window->getSdlWindow())
    {
        SDL_RaiseWindow(window->getSdlWindow());
    }
}

void NormalizeWindow::layoutComponents() const
{
    if (!window || !window->getRootComponent())
    {
        return;
    }

    float canvasW = static_cast<float>(kWindowWidth);
    float canvasH = static_cast<float>(kWindowHeight);
    if (window->getCanvas())
    {
        SDL_GetTextureSize(window->getCanvas(), &canvasW, &canvasH);
    }

    const int canvasWi = static_cast<int>(canvasW);
    const int canvasHi = static_cast<int>(canvasH);
    const int padding = std::max(6, scaleUi(state, 16.0f));
    const int rowHeight = std::max(32, scaleUi(state, 38.0f));
    const int buttonWidth = std::max(96, scaleUi(state, 120.0f));

    window->getRootComponent()->setSize(canvasWi, canvasHi);
    background->setBounds(0, 0, canvasWi, canvasHi);
    messageLabel->setBounds(padding, padding, canvasWi - padding * 2,
                            rowHeight * 2);
    applyButton->setBounds(canvasWi - padding - buttonWidth,
                           canvasHi - padding - rowHeight, buttonWidth, rowHeight);
    cancelButton->setBounds(canvasWi - padding * 2 - buttonWidth * 2,
                            canvasHi - padding - rowHeight, buttonWidth, rowHeight);
}

void NormalizeWindow::closeNow()
{
    if (window)
    {
        window->requestClose();
    }
}
