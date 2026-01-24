#include <catch2/catch_test_macros.hpp>
#include "gui/Component.h"

#include <iostream>

TEST_CASE("MenuBar dirty rect intersection check")
{
    cupuacu::State state;
    state.pixelScale = 1;
    state.menuFontSize = 12;
    state.window = nullptr;
    state.renderer = nullptr;
    state.canvas = nullptr;

    // Build a simplified tree
    state.rootComponent = std::make_unique<Component>(&state, "RootComponent");
    state.rootComponent->setSize(800, 600);

    auto menuBar = std::make_unique<Component>(&state, "MenuBar");
    auto *mb = state.rootComponent->addChild(menuBar);

    // Place MenuBar at top of window
    mb->setBounds(0, 0, 800, 20);

    // Simulate a dirty rect far below the menu
    SDL_Rect dr{100, 100, 50, 50};
    state.dirtyRects.push_back(dr);

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
