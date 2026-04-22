#include <catch2/catch_test_macros.hpp>

#include "gui/WindowPlacementPlanning.hpp"

TEST_CASE("Initial window placement uses persisted position when it intersects a display",
          "[gui]")
{
    const auto plan = cupuacu::gui::planInitialWindowPlacement(
        100, 120, 800, 600, {SDL_Rect{0, 0, 1920, 1080}});

    REQUIRE(plan.usePersistedPosition);
    REQUIRE(plan.x == 100);
    REQUIRE(plan.y == 120);
}

TEST_CASE("Initial window placement rejects persisted position when it is fully off-screen",
          "[gui]")
{
    const auto plan = cupuacu::gui::planInitialWindowPlacement(
        5000, 5000, 800, 600, {SDL_Rect{0, 0, 1920, 1080}});

    REQUIRE_FALSE(plan.usePersistedPosition);
}

TEST_CASE("Initial window placement tolerates missing display bounds by using persisted position",
          "[gui]")
{
    const auto plan =
        cupuacu::gui::planInitialWindowPlacement(-1400, 80, 800, 600, {});

    REQUIRE(plan.usePersistedPosition);
    REQUIRE(plan.x == -1400);
    REQUIRE(plan.y == 80);
}
