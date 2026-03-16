#include <catch2/catch_test_macros.hpp>

#include "gui/ComponentPlanning.hpp"

TEST_CASE("Component dirty planning marks subtree only", "[gui]")
{
    std::vector<SDL_Rect> dirtyRects;
    cupuacu::gui::appendVisibleDirtySubtreeRects(
        SDL_Rect{10, 10, 100, 100}, true,
        {{SDL_Rect{15, 15, 30, 30}, true},
         {SDL_Rect{50, 50, 30, 30}, true},
         {SDL_Rect{120, 10, 50, 50}, false}},
        dirtyRects);

    REQUIRE(dirtyRects.size() == 3);
    REQUIRE(dirtyRects[0].x == 10);
    REQUIRE(dirtyRects[1].x == 15);
    REQUIRE(dirtyRects[2].x == 50);
}

TEST_CASE("Component bounds planning identifies when parent must also be dirtied",
          "[gui]")
{
    REQUIRE(cupuacu::gui::shouldDirtyParentAfterBoundsChange(
        SDL_Rect{0, 0, 20, 20}, SDL_Rect{5, 5, 30, 30}, true));
    REQUIRE_FALSE(cupuacu::gui::shouldDirtyParentAfterBoundsChange(
        SDL_Rect{0, 0, 20, 20}, SDL_Rect{0, 0, 20, 20}, true));
    REQUIRE_FALSE(cupuacu::gui::shouldDirtyParentAfterBoundsChange(
        SDL_Rect{0, 0, 20, 20}, SDL_Rect{5, 5, 30, 30}, false));
}

TEST_CASE("Component reorder planning sends to back and brings to front",
          "[gui]")
{
    REQUIRE(cupuacu::gui::reorderIndexForSendToBack(1, 3) == 0);
    REQUIRE(cupuacu::gui::reorderIndexForBringToFront(1, 3) == 2);
}

TEST_CASE("Component clipping planning respects parent clipping flag", "[gui]")
{
    const SDL_Rect parent{10, 10, 20, 20};
    const SDL_Rect child{5, 5, 40, 40};

    REQUIRE_FALSE(
        cupuacu::gui::containsAbsoluteCoordinateWithOptionalParentClipping(
            child, true, parent, 8, 8));
    REQUIRE(
        cupuacu::gui::containsAbsoluteCoordinateWithOptionalParentClipping(
            child, false, parent, 8, 8));
}
