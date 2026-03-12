#include <catch2/catch_test_macros.hpp>

#include "gui/WindowMouseRouting.hpp"

TEST_CASE("Window mouse routing helper plans hover and capture transitions",
          "[gui]")
{
    SECTION("move without capture refreshes hover after dispatch")
    {
        const auto plan = cupuacu::gui::planWindowMouseRouting(
            cupuacu::gui::MOVE, true, false, false);

        REQUIRE(plan.handled);
        REQUIRE(plan.dispatchToRoot);
        REQUIRE_FALSE(plan.updateHoverBeforeDispatch);
        REQUIRE(plan.updateHoverAfterDispatch);
        REQUIRE_FALSE(plan.sendLeaveToCaptureBeforeDispatch);
        REQUIRE_FALSE(plan.clearCaptureAfterDispatch);
    }

    SECTION("move with capture skips hover refresh")
    {
        const auto plan = cupuacu::gui::planWindowMouseRouting(
            cupuacu::gui::MOVE, true, true, true);

        REQUIRE(plan.handled);
        REQUIRE(plan.dispatchToRoot);
        REQUIRE_FALSE(plan.updateHoverBeforeDispatch);
        REQUIRE_FALSE(plan.updateHoverAfterDispatch);
        REQUIRE_FALSE(plan.sendLeaveToCaptureBeforeDispatch);
        REQUIRE_FALSE(plan.clearCaptureAfterDispatch);
    }

    SECTION("mouse up refreshes hover and releases capture")
    {
        const auto plan = cupuacu::gui::planWindowMouseRouting(
            cupuacu::gui::UP, true, true, false);

        REQUIRE(plan.handled);
        REQUIRE(plan.dispatchToRoot);
        REQUIRE(plan.updateHoverBeforeDispatch);
        REQUIRE_FALSE(plan.updateHoverAfterDispatch);
        REQUIRE(plan.sendLeaveToCaptureBeforeDispatch);
        REQUIRE(plan.clearCaptureAfterDispatch);
    }

    SECTION("mouse up does not synthesize leave when capture still contains cursor")
    {
        const auto plan = cupuacu::gui::planWindowMouseRouting(
            cupuacu::gui::UP, true, true, true);

        REQUIRE(plan.handled);
        REQUIRE(plan.dispatchToRoot);
        REQUIRE(plan.updateHoverBeforeDispatch);
        REQUIRE_FALSE(plan.sendLeaveToCaptureBeforeDispatch);
        REQUIRE(plan.clearCaptureAfterDispatch);
    }

    SECTION("wheel refreshes hover before dispatch")
    {
        const auto plan = cupuacu::gui::planWindowMouseRouting(
            cupuacu::gui::WHEEL, true, false, false);

        REQUIRE(plan.handled);
        REQUIRE(plan.dispatchToRoot);
        REQUIRE(plan.updateHoverBeforeDispatch);
        REQUIRE_FALSE(plan.updateHoverAfterDispatch);
        REQUIRE_FALSE(plan.sendLeaveToCaptureBeforeDispatch);
        REQUIRE_FALSE(plan.clearCaptureAfterDispatch);
    }

    SECTION("no root means event is ignored")
    {
        const auto plan = cupuacu::gui::planWindowMouseRouting(
            cupuacu::gui::DOWN, false, false, false);

        REQUIRE_FALSE(plan.handled);
        REQUIRE_FALSE(plan.dispatchToRoot);
        REQUIRE_FALSE(plan.updateHoverBeforeDispatch);
        REQUIRE_FALSE(plan.updateHoverAfterDispatch);
        REQUIRE_FALSE(plan.sendLeaveToCaptureBeforeDispatch);
        REQUIRE_FALSE(plan.clearCaptureAfterDispatch);
    }
}
