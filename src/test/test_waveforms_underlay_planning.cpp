#include <catch2/catch_test_macros.hpp>

#include "gui/WaveformsUnderlayPlanning.hpp"

TEST_CASE("WaveformsUnderlay single click planning uses hovered sample position",
          "[gui]")
{
    const double samplePos =
        cupuacu::gui::planWaveformsUnderlaySamplePosition(10, 1.0, 7.0f);
    REQUIRE(samplePos == 17.0);
}

TEST_CASE("WaveformsUnderlay double click planning selects visible range",
          "[gui]")
{
    double start = 0.0;
    double end = 0.0;
    REQUIRE(cupuacu::gui::planWaveformsUnderlayVisibleRangeSelection(
        200, 25, 1.0, 50, start, end));
    REQUIRE(start == 25.0);
    REQUIRE(end == 75.0);
}

TEST_CASE("WaveformsUnderlay drag planning updates selection and right-channel hover",
          "[gui]")
{
    cupuacu::gui::Selection<double> selection(0.0);
    selection.setHighest(100.0);
    selection.setValue1(10.0);

    cupuacu::gui::applyWaveformsUnderlayDraggedSelection(selection, 0, 1.0, 20);

    REQUIRE(selection.isActive());
    REQUIRE(selection.getStartInt() == 10);
    REQUIRE(selection.getEndInt() == 19);
    REQUIRE(cupuacu::gui::planWaveformsUnderlayHoveredChannels(79, 80, 2) ==
            cupuacu::RIGHT);
}

TEST_CASE("WaveformsUnderlay mono hover planning keeps channel selection unified",
          "[gui]")
{
    REQUIRE(cupuacu::gui::planWaveformsUnderlayHoveredChannels(0, 80, 1) ==
            cupuacu::BOTH);
    REQUIRE(cupuacu::gui::planWaveformsUnderlayHoveredChannels(79, 80, 1) ==
            cupuacu::BOTH);
}
