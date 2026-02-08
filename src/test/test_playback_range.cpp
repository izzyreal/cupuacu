#include <catch2/catch_test_macros.hpp>

#include "DocumentSession.hpp"
#include "playback/PlaybackRange.hpp"

TEST_CASE("Playback helper computes initial play range", "[session]")
{
    cupuacu::DocumentSession session{};
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 100);

    SECTION("Selection wins")
    {
        session.cursor = 90;
        session.selection.setValue1(10.0);
        session.selection.setValue2(31.0); // [10, 31)
        const auto range =
            cupuacu::playback::computeRangeForPlay(session, false);
        REQUIRE(range.start == 10);
        REQUIRE(range.end == 31);
    }

    SECTION("No selection, no loop starts at cursor")
    {
        session.selection.reset();
        session.cursor = 25;
        const auto range =
            cupuacu::playback::computeRangeForPlay(session, false);
        REQUIRE(range.start == 25);
        REQUIRE(range.end == 100);
    }

    SECTION("No selection, loop starts at cursor")
    {
        session.selection.reset();
        session.cursor = 40;
        const auto range =
            cupuacu::playback::computeRangeForPlay(session, true);
        REQUIRE(range.start == 40);
        REQUIRE(range.end == 100);
    }
}

TEST_CASE("Playback helper computes live-update range", "[session]")
{
    cupuacu::DocumentSession session{};
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 120);

    SECTION("Selection overrides fallback")
    {
        session.selection.setValue1(12.0);
        session.selection.setValue2(24.0); // [12, 24)
        const auto range = cupuacu::playback::computeRangeForLiveUpdate(
            session, false, 1, 2);
        REQUIRE(range.start == 12);
        REQUIRE(range.end == 24);
    }

    SECTION("No selection and loop uses whole document")
    {
        session.selection.reset();
        const auto range = cupuacu::playback::computeRangeForLiveUpdate(
            session, true, 35, 80);
        REQUIRE(range.start == 0);
        REQUIRE(range.end == 120);
    }

    SECTION("No selection and no loop keeps fallback")
    {
        session.selection.reset();
        const auto range = cupuacu::playback::computeRangeForLiveUpdate(
            session, false, 30, 70);
        REQUIRE(range.start == 30);
        REQUIRE(range.end == 70);
    }
}
