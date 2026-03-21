#include <catch2/catch_test_macros.hpp>

#include "gui/TooltipPlanning.hpp"

TEST_CASE("Tooltip canvas rect mapping accounts for canvas scale and window position",
          "[gui]")
{
    const SDL_Rect canvasRect{900, 36, 200, 24};
    const SDL_Rect parentWindowBounds{300, 120, 1200, 800};
    const SDL_FPoint canvasSize{2400.0f, 1600.0f};

    const auto screenRect = cupuacu::gui::mapCanvasRectToScreenRect(
        canvasRect, parentWindowBounds, canvasSize);

    REQUIRE(screenRect.x == 750);
    REQUIRE(screenRect.y == 138);
    REQUIRE(screenRect.w == 100);
    REQUIRE(screenRect.h == 12);
}

TEST_CASE("Tooltip canvas rect mapping rejects invalid geometry", "[gui]")
{
    const auto screenRect = cupuacu::gui::mapCanvasRectToScreenRect(
        SDL_Rect{100, 20, 80, 20}, SDL_Rect{300, 120, 1200, 800},
        SDL_FPoint{0.0f, 1600.0f});

    REQUIRE(screenRect.w == 0);
    REQUIRE(screenRect.h == 0);
}

TEST_CASE("Tooltip popup planning places tooltips below the anchor when room exists",
          "[gui]")
{
    const SDL_Rect parentBounds{100, 100, 800, 600};
    const SDL_Rect anchorBounds{150, 150, 100, 24};
    const SDL_Rect displayBounds{0, 0, 1920, 1080};

    const auto plan = cupuacu::gui::planTooltipPopupPlacement(
        parentBounds, anchorBounds, displayBounds, 200, 40, 8);

    REQUIRE(plan.valid);
    REQUIRE(plan.popupX == 100);
    REQUIRE(plan.popupY == 182);
    REQUIRE(plan.offsetX == 0);
    REQUIRE(plan.offsetY == 82);
}

TEST_CASE("Tooltip popup planning flips above near the bottom edge",
          "[gui]")
{
    const SDL_Rect parentBounds{100, 100, 800, 600};
    const SDL_Rect anchorBounds{300, 740, 120, 24};
    const SDL_Rect displayBounds{0, 0, 1280, 800};

    const auto plan = cupuacu::gui::planTooltipPopupPlacement(
        parentBounds, anchorBounds, displayBounds, 220, 44, 10);

    REQUIRE(plan.valid);
    REQUIRE(plan.popupY == 686);
    REQUIRE(plan.offsetY == 586);
}

TEST_CASE("Tooltip popup planning clamps horizontally to the display",
          "[gui]")
{
    const SDL_Rect parentBounds{500, 100, 800, 600};
    const SDL_Rect anchorBounds{520, 150, 80, 24};
    const SDL_Rect displayBounds{0, 0, 640, 480};

    const auto plan = cupuacu::gui::planTooltipPopupPlacement(
        parentBounds, anchorBounds, displayBounds, 300, 40, 8);

    REQUIRE(plan.valid);
    REQUIRE(plan.popupX == 340);
    REQUIRE(plan.offsetX == -160);
}

TEST_CASE("Tooltip popup geometry scales logical size with pixel scale",
          "[gui]")
{
    const auto geometry = cupuacu::gui::planTooltipPopupGeometry(
        90, 10, 1.0f, 5, 4, 2, 1920, 1080);

    REQUIRE(geometry.valid);
    REQUIRE(geometry.canvasWidth == 100);
    REQUIRE(geometry.canvasHeight == 20);
    REQUIRE(geometry.logicalWidth == 200);
    REQUIRE(geometry.logicalHeight == 40);
    REQUIRE(geometry.gapLogical == 8);
    REQUIRE(geometry.renderPaddingPx == 5);
}

TEST_CASE("Tooltip popup geometry preserves integer upscale when clamped",
          "[gui]")
{
    const auto geometry = cupuacu::gui::planTooltipPopupGeometry(
        150, 12, 1.0f, 5, 4, 4, 500, 300);

    REQUIRE(geometry.valid);
    REQUIRE(geometry.canvasWidth == 125);
    REQUIRE(geometry.logicalWidth == 500);
    REQUIRE(geometry.canvasHeight == 22);
    REQUIRE(geometry.logicalHeight == 88);
    REQUIRE(geometry.gapLogical == 16);
}
