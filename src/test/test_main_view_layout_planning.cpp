#include <catch2/catch_test_macros.hpp>

#include "gui/MainViewLayoutPlanning.hpp"

#include <cmath>

TEST_CASE("MainView layout keeps scrollbar physical height stable across pixel scales",
          "[gui]")
{
    const auto plan1 = cupuacu::gui::planMainViewLayout(800, 400, 1.0f, 1);
    const auto plan2 = cupuacu::gui::planMainViewLayout(400, 200, 1.0f, 2);
    const auto plan4 = cupuacu::gui::planMainViewLayout(200, 100, 1.0f, 4);

    const int physical1 = plan1.scrollBarHeight * 1;
    const int physical2 = plan2.scrollBarHeight * 2;
    const int physical4 = plan4.scrollBarHeight * 4;

    REQUIRE(std::abs(physical2 - physical1) <= 2);
    REQUIRE(std::abs(physical4 - physical1) <= 2);
    REQUIRE(plan4.scrollBarHeight < plan2.scrollBarHeight);
    REQUIRE(plan2.scrollBarHeight < plan1.scrollBarHeight);
}

TEST_CASE("MainView layout tiles waveform channels without gaps", "[gui]")
{
    const auto layout = cupuacu::gui::planMainViewLayout(801, 303, 1.0f, 4);
    const auto tiles =
        cupuacu::gui::planWaveformChannelTiles(layout.waveforms.w,
                                               layout.waveforms.h, 2);

    REQUIRE(tiles.size() == 2);
    REQUIRE(tiles[0].y == 0);
    REQUIRE(tiles[0].y + tiles[0].h == tiles[1].y);
    REQUIRE(tiles[1].y + tiles[1].h == layout.waveforms.h);
}

TEST_CASE("MainView layout keeps timeline height close to label-plus-tick space",
          "[gui]")
{
    const auto plan = cupuacu::gui::planMainViewLayout(800, 400, 1.0f, 1);

    REQUIRE(plan.timelineHeight == 44);
}
