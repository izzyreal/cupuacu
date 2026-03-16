#include <catch2/catch_test_macros.hpp>

#include "TestSdlTtfGuard.hpp"
#include "gui/Component.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/Window.hpp"
#include "State.hpp"

#include <algorithm>

using namespace cupuacu::gui;

static std::unique_ptr<Window> makeTestWindow(cupuacu::State *state)
{
    cupuacu::test::ensureSdlTtfInitialized();
    auto window =
        std::make_unique<Window>(state, "test", 320, 240, SDL_WINDOW_HIDDEN);
    REQUIRE(window->isOpen());
    return window;
}

// helper to check if a rect exists in a vector
static bool contains_rect(const std::vector<SDL_Rect> &vec, const SDL_Rect &r)
{
    return std::any_of(vec.begin(), vec.end(),
                       [&](const SDL_Rect &e)
                       {
                           return e.x == r.x && e.y == r.y && e.w == r.w &&
                                  e.h == r.h;
                       });
}

// helper to construct SDL_Rect easily
static SDL_Rect makeRect(int x, int y, int w, int h)
{
    return SDL_Rect{x, y, w, h};
}

TEST_CASE(
    "parent setDirty marks itself and children only (no ancestors or siblings)",
    "[dirty]")
{
    cupuacu::State state{};
    auto window = makeTestWindow(&state);

    Component root(&state, "root");
    root.setWindow(window.get());
    root.setVisible(true);
    root.setBounds(0, 0, 200, 200);

    Component *a = root.emplaceChild<Component>(&state, "A");
    a->setBounds(10, 10, 100, 100);

    Component *a1 = a->emplaceChild<Component>(&state, "A1");
    a1->setBounds(5, 5, 30, 30);

    Component *a2 = a->emplaceChild<Component>(&state, "A2");
    a2->setBounds(40, 40, 30, 30);

    Component *b = root.emplaceChild<Component>(&state, "B");
    b->setBounds(120, 10, 50, 50);

    window->getDirtyRects().clear();

    a->setDirty();

    REQUIRE(contains_rect(window->getDirtyRects(), a->getAbsoluteBounds()));
    REQUIRE(contains_rect(window->getDirtyRects(), a1->getAbsoluteBounds()));
    REQUIRE(contains_rect(window->getDirtyRects(), a2->getAbsoluteBounds()));
    REQUIRE(!contains_rect(window->getDirtyRects(), root.getAbsoluteBounds()));
    REQUIRE(!contains_rect(window->getDirtyRects(), b->getAbsoluteBounds()));
}

TEST_CASE("setBounds records new bounds", "[bounds]")
{
    cupuacu::State state{};
    auto window = makeTestWindow(&state);

    Component c(&state, "comp");
    c.setWindow(window.get());
    c.setVisible(true);
    c.setBounds(0, 0, 20, 20);

    window->getDirtyRects().clear();

    c.setBounds(5, 5, 30, 30);

    SDL_Rect oldRect = makeRect(0, 0, 20, 20);
    SDL_Rect newRect = makeRect(5, 5, 30, 30);

    REQUIRE(window->getDirtyRects().size() >= 1);
    REQUIRE(contains_rect(window->getDirtyRects(), newRect));
}

TEST_CASE("Menu bar area does not intersect unrelated dirty rect", "[dirty]")
{
    cupuacu::State state{};
    state.pixelScale = 1;
    state.menuFontSize = 12;

    auto window =
        std::make_unique<Window>(&state, "test", 800, 600, SDL_WINDOW_HIDDEN);

    Component root(&state, "RootComponent");
    root.setWindow(window.get());
    root.setSize(800, 600);

    auto menuBar = std::make_unique<Component>(&state, "MenuBar");
    auto *mb = root.addChild(menuBar);
    mb->setBounds(0, 0, 800, 20);

    SDL_Rect dirtyRect{100, 100, 50, 50};
    window->getDirtyRects().push_back(dirtyRect);

    const SDL_Rect menuBounds = mb->getAbsoluteBounds();
    SDL_Rect intersection{};
    const bool hasIntersection =
        SDL_GetRectIntersection(&menuBounds, &dirtyRect, &intersection);

    REQUIRE(!hasIntersection);
}
