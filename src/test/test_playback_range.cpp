#include <catch2/catch_test_macros.hpp>

#include "DocumentSession.hpp"
#include "playback/PlaybackRange.hpp"

TEST_CASE("Playback helper initial range uses selection", "[session]")
{
    cupuacu::DocumentSession session{};
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 100);

    session.cursor = 90;
    session.selection.setValue1(10.0);
    session.selection.setValue2(31.0); // [10, 31)
    const auto range = cupuacu::playback::computeRangeForPlay(session, false);
    REQUIRE(range.start == 10);
    REQUIRE(range.end == 31);
}

TEST_CASE("Playback helper initial range without selection starts at cursor", "[session]")
{
    cupuacu::DocumentSession session{};
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 100);

    session.selection.reset();
    session.cursor = 25;
    const auto range = cupuacu::playback::computeRangeForPlay(session, false);
    REQUIRE(range.start == 25);
    REQUIRE(range.end == 100);
}

TEST_CASE("Playback helper initial loop range without selection starts at cursor", "[session]")
{
    cupuacu::DocumentSession session{};
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 100);

    session.selection.reset();
    session.cursor = 40;
    const auto range = cupuacu::playback::computeRangeForPlay(session, true);
    REQUIRE(range.start == 40);
    REQUIRE(range.end == 100);
}

TEST_CASE("Playback helper live-update range uses selection", "[session]")
{
    cupuacu::DocumentSession session{};
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 120);

    session.selection.setValue1(12.0);
    session.selection.setValue2(24.0); // [12, 24)
    const auto range =
        cupuacu::playback::computeRangeForLiveUpdate(session, false, 1, 2);
    REQUIRE(range.start == 12);
    REQUIRE(range.end == 24);
}

TEST_CASE("Playback helper live-update loop range without selection uses whole doc", "[session]")
{
    cupuacu::DocumentSession session{};
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 120);

    session.selection.reset();
    const auto range =
        cupuacu::playback::computeRangeForLiveUpdate(session, true, 35, 80);
    REQUIRE(range.start == 0);
    REQUIRE(range.end == 120);
}

TEST_CASE("Playback helper live-update range without selection keeps fallback", "[session]")
{
    cupuacu::DocumentSession session{};
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 120);

    session.selection.reset();
    const auto range =
        cupuacu::playback::computeRangeForLiveUpdate(session, false, 30, 70);
    REQUIRE(range.start == 30);
    REQUIRE(range.end == 70);
}
