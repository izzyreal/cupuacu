#include <catch2/catch_test_macros.hpp>
#include "gui/Component.h"
#include "gui/DevicePropertiesWindow.h"
#include "gui/Window.h"
#include "State.h"

#include <iostream>

using namespace cupuacu::gui;

TEST_CASE("MenuBar dirty rect intersection check")
{
    cupuacu::State state{};
    state.pixelScale = 1;
    state.menuFontSize = 12;

    auto window =
        std::make_unique<Window>(&state, "test", 800, 600, SDL_WINDOW_HIDDEN);

    // Build a simplified tree
    Component root(&state, "RootComponent");
    root.setWindow(window.get());
    root.setSize(800, 600);

    auto menuBar = std::make_unique<Component>(&state, "MenuBar");
    auto *mb = root.addChild(menuBar);

    // Place MenuBar at top of window
    mb->setBounds(0, 0, 800, 20);

    // Simulate a dirty rect far below the menu
    SDL_Rect dr{100, 100, 50, 50};
    window->getDirtyRects().push_back(dr);

    SDL_Rect mbRect = mb->getAbsoluteBounds();

    // Logging
    std::cout << "MenuBar rect: " << mbRect.x << "," << mbRect.y << " "
              << mbRect.w << "x" << mbRect.h << "\n";
    std::cout << "Dirty rect:   " << dr.x << "," << dr.y << " " << dr.w << "x"
              << dr.h << "\n";

    SDL_Rect intersection;
    bool hasIntersection = SDL_GetRectIntersection(&mbRect, &dr, &intersection);

    if (hasIntersection)
    {
        std::cout << "Intersection: " << intersection.x << "," << intersection.y
                  << " " << intersection.w << "x" << intersection.h << "\n";
    }
    else
    {
        std::cout << "No intersection.\n";
    }

    REQUIRE(!hasIntersection);
}
