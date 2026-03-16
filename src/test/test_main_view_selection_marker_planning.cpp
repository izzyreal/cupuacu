#include <catch2/catch_test_macros.hpp>

#include "gui/MainViewSelectionMarkerPlanning.hpp"

TEST_CASE("MainView selection marker planning places start and end markers correctly",
          "[gui]")
{
    const auto startTop =
        cupuacu::gui::planTopSelectionStartMarker(20, 16, 14, 12.0f);
    const auto startBottom =
        cupuacu::gui::planBottomSelectionStartMarker(20, 16, 12.0f);
    const auto endTop =
        cupuacu::gui::planTopSelectionEndMarker(50, 16, 14, 12.0f);
    const auto endBottom =
        cupuacu::gui::planBottomSelectionEndMarker(50, 16, 12.0f);

    REQUIRE(startTop.visible);
    REQUIRE(startTop.rect.x == 36);
    REQUIRE(startTop.rect.y == 14);
    REQUIRE(startTop.rect.w == 13);
    REQUIRE(startTop.rect.h == 12);

    REQUIRE(startBottom.visible);
    REQUIRE(startBottom.rect.x == 36);
    REQUIRE(startBottom.rect.y == 0);
    REQUIRE(startBottom.rect.w == 13);
    REQUIRE(startBottom.rect.h == 12);

    REQUIRE(endTop.visible);
    REQUIRE(endTop.rect.x == 54);
    REQUIRE(endTop.rect.y == 14);
    REQUIRE(endTop.rect.w == 12);
    REQUIRE(endTop.rect.h == 12);

    REQUIRE(endBottom.visible);
    REQUIRE(endBottom.rect.x == 54);
    REQUIRE(endBottom.rect.y == 0);
    REQUIRE(endBottom.rect.w == 12);
    REQUIRE(endBottom.rect.h == 12);
}

TEST_CASE("MainView cursor marker planning centers around the cursor sample",
          "[gui]")
{
    const auto top =
        cupuacu::gui::planTopCursorMarker(25, 16, 14, 12.0f, 12.0f);
    const auto bottom =
        cupuacu::gui::planBottomCursorMarker(25, 16, 12.0f, 12.0f);

    REQUIRE(top.visible);
    REQUIRE(top.rect.x == 30);
    REQUIRE(top.rect.y == 14);
    REQUIRE(top.rect.w == 24);
    REQUIRE(top.rect.h == 12);

    REQUIRE(bottom.visible);
    REQUIRE(bottom.rect.x == 30);
    REQUIRE(bottom.rect.y == 0);
    REQUIRE(bottom.rect.w == 24);
    REQUIRE(bottom.rect.h == 12);
}
