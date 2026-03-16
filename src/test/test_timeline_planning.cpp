#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "gui/TimelinePlanning.hpp"

using Catch::Approx;

TEST_CASE("Timeline planning refreshes only when zoom or offset changes", "[gui]")
{
    REQUIRE_FALSE(cupuacu::gui::planTimelineNeedsRefresh(2.0, 10, 2.0, 10));
    REQUIRE(cupuacu::gui::planTimelineNeedsRefresh(2.0, 10, 3.0, 10));
    REQUIRE(cupuacu::gui::planTimelineNeedsRefresh(2.0, 10, 2.0, 11));
}

TEST_CASE("Timeline ruler planning builds sample labels with nice spacing", "[gui]")
{
    const auto plan = cupuacu::gui::planTimelineRuler(
        1000, 1, 0, 100.0, 48000,
        cupuacu::gui::TimelinePlanningMode::Samples, false);

    REQUIRE(plan.valid);
    REQUIRE(plan.labels.size() >= 2);
    REQUIRE(plan.labels.front() == "smpl");
    REQUIRE(plan.labels[1] == "10000");
    REQUIRE(plan.tickSpacingPx == Approx(100.0f));
    REQUIRE(plan.subdivisions == 5);
    REQUIRE(plan.scrollOffsetPx == Approx(0.0f));
}

TEST_CASE("Timeline ruler planning formats decimal labels from sample rate", "[gui]")
{
    const auto plan = cupuacu::gui::planTimelineRuler(
        1000, 1, 0, 10.0, 1000,
        cupuacu::gui::TimelinePlanningMode::Decimal, false);

    REQUIRE(plan.valid);
    REQUIRE(plan.labels.size() >= 2);
    REQUIRE(plan.labels[1] == "0:1.000");
}

TEST_CASE("Timeline ruler planning rounds scroll offset at sample-point zoom", "[gui]")
{
    const auto lowZoomPlan = cupuacu::gui::planTimelineRuler(
        200, 1, 105, 10.0, 48000,
        cupuacu::gui::TimelinePlanningMode::Samples, false);
    const auto highZoomPlan = cupuacu::gui::planTimelineRuler(
        200, 1, 105, 10.0, 48000,
        cupuacu::gui::TimelinePlanningMode::Samples, true);

    REQUIRE(lowZoomPlan.valid);
    REQUIRE(highZoomPlan.valid);
    REQUIRE(lowZoomPlan.scrollOffsetPx == Approx(-10.5f));
    REQUIRE(highZoomPlan.scrollOffsetPx == Approx(-11.0f));
}
