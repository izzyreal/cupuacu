#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "actions/ZoomPlanning.hpp"

TEST_CASE("Reset zoom plan derives samples-per-pixel from waveform width",
          "[gui]")
{
    const auto plan = cupuacu::actions::planResetZoom(1000, 200);

    REQUIRE(plan.samplesPerPixel == Catch::Approx(5.0));
    REQUIRE(plan.verticalZoom == Catch::Approx(cupuacu::INITIAL_VERTICAL_ZOOM));
    REQUIRE(plan.sampleOffset == 0);
}

TEST_CASE("Reset zoom plan handles zero-width waveform safely", "[gui]")
{
    const auto plan = cupuacu::actions::planResetZoom(1000, 0);

    REQUIRE(plan.samplesPerPixel == Catch::Approx(0.0));
    REQUIRE(plan.verticalZoom == Catch::Approx(cupuacu::INITIAL_VERTICAL_ZOOM));
    REQUIRE(plan.sampleOffset == 0);
}

TEST_CASE("Horizontal zoom-in plan keeps the view centered and respects bounds",
          "[gui]")
{
    const auto plan =
        cupuacu::actions::planZoomInHorizontally(4.0, 100, 200, 1000);

    REQUIRE(plan.changed);
    REQUIRE(plan.samplesPerPixel == Catch::Approx(2.0));
    REQUIRE(plan.sampleOffset == 301);
}

TEST_CASE("Horizontal zoom-out plan keeps the view centered and respects bounds",
          "[gui]")
{
    const auto plan =
        cupuacu::actions::planZoomOutHorizontally(2.0, 301, 200, 1000);

    REQUIRE(plan.changed);
    REQUIRE(plan.samplesPerPixel == Catch::Approx(4.0));
    REQUIRE(plan.sampleOffset == 100);
}

TEST_CASE("Horizontal zoom plans clamp at min and max samples-per-pixel",
          "[gui]")
{
    const auto inPlan = cupuacu::actions::planZoomInHorizontally(
        1.0 / 200.0, 0, 200, 1000);
    const auto outPlan =
        cupuacu::actions::planZoomOutHorizontally(5.0, 0, 200, 1000);

    REQUIRE_FALSE(inPlan.changed);
    REQUIRE_FALSE(outPlan.changed);
}

TEST_CASE("Zoom selection plan focuses the selected range", "[gui]")
{
    const auto plan =
        cupuacu::actions::planZoomSelection(true, 200, 100, 250);

    REQUIRE(plan.changed);
    REQUIRE(plan.samplesPerPixel == Catch::Approx(200.0 / 250.0));
    REQUIRE(plan.verticalZoom == Catch::Approx(cupuacu::INITIAL_VERTICAL_ZOOM));
    REQUIRE(plan.sampleOffset == 100);
}

TEST_CASE("Zoom selection plan rejects inactive and empty selections", "[gui]")
{
    REQUIRE_FALSE(
        cupuacu::actions::planZoomSelection(false, 200, 100, 250).changed);
    REQUIRE_FALSE(
        cupuacu::actions::planZoomSelection(true, 0, 100, 250).changed);
    REQUIRE_FALSE(
        cupuacu::actions::planZoomSelection(true, 200, 100, 0).changed);
}
