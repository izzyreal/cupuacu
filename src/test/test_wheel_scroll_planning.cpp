#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "gui/WaveformsUnderlayPlanning.hpp"

using Catch::Approx;

TEST_CASE("WaveformsUnderlay autoscroll planning scrolls left and right outside bounds",
          "[gui]")
{
    const auto leftPlan =
        cupuacu::gui::planWaveformsUnderlayAutoScroll(-3, 100, 2.0);
    const auto insidePlan =
        cupuacu::gui::planWaveformsUnderlayAutoScroll(50, 100, 2.0);
    const auto rightPlan =
        cupuacu::gui::planWaveformsUnderlayAutoScroll(104, 100, 2.0);

    REQUIRE(leftPlan.samplesToScroll == -6.0);
    REQUIRE(insidePlan.samplesToScroll == 0.0);
    REQUIRE(rightPlan.samplesToScroll == 8.0);
}

TEST_CASE("WaveformsUnderlay wheel step planning snaps small deltas and smooths larger ones",
          "[gui]")
{
    const auto smallPlan =
        cupuacu::gui::planWaveformsUnderlayWheelStep(0.25, 100, 110);
    const auto smoothPlan =
        cupuacu::gui::planWaveformsUnderlayWheelStep(10.0, 100, 110);
    const auto timedOutPlan =
        cupuacu::gui::planWaveformsUnderlayWheelStep(10.0, 100, 121);

    REQUIRE(smallPlan.stepPixels == 0.25);
    REQUIRE(smallPlan.remainingPendingPixels == 0.0);

    REQUIRE(smoothPlan.stepPixels == Approx(3.0));
    REQUIRE(smoothPlan.remainingPendingPixels == Approx(7.0));

    REQUIRE(timedOutPlan.stepPixels == Approx(10.0));
    REQUIRE(timedOutPlan.remainingPendingPixels == 0.0);
}

TEST_CASE("WaveformsUnderlay wheel delta planning preserves fractional remainders",
          "[gui]")
{
    const auto deltaPlan =
        cupuacu::gui::planWaveformsUnderlayWheelDelta(0.25, 1.5, 2.0);

    REQUIRE(deltaPlan.wholeSamples == 3);
    REQUIRE(deltaPlan.remainingSamples == Approx(0.25));
}

TEST_CASE("WaveformsUnderlay wheel delta planning keeps sub-sample movement pending",
          "[gui]")
{
    const auto deltaPlan =
        cupuacu::gui::planWaveformsUnderlayWheelDelta(0.1, 0.2, 2.0);

    REQUIRE(deltaPlan.wholeSamples == 0);
    REQUIRE(deltaPlan.remainingSamples == Approx(0.5));
}
