#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "actions/audio/EditCommands.hpp"
#include "gui/DevicePropertiesWindow.hpp"

TEST_CASE("Edit command selection target is inactive without selection", "[session]")
{
    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 128);

    session.selection.reset();
    const auto target = cupuacu::actions::audio::selectionTarget(&state);
    REQUIRE(target.start == 0);
    REQUIRE(target.length == 0);
}

TEST_CASE("Edit command selection target uses active selection bounds", "[session]")
{
    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 128);

    session.selection.setValue1(11.0);
    session.selection.setValue2(19.0); // [11, 19)
    const auto target = cupuacu::actions::audio::selectionTarget(&state);
    REQUIRE(target.start == 11);
    REQUIRE(target.length == 8);
}

TEST_CASE("Edit command paste target without selection uses cursor", "[session]")
{
    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 128);

    session.selection.reset();
    session.cursor = 33;
    const auto target = cupuacu::actions::audio::pasteTarget(&state);
    REQUIRE(target.start == 33);
    REQUIRE(target.end == -1);
}

TEST_CASE("Edit command paste target with selection uses selection bounds", "[session]")
{
    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 128);

    session.selection.setValue1(7.0);
    session.selection.setValue2(15.0); // inclusive integer selection [7, 14]
    const auto target = cupuacu::actions::audio::pasteTarget(&state);
    REQUIRE(target.start == 7);
    REQUIRE(target.end == 15);
}

TEST_CASE("Edit command insert silence inserts at cursor without replacing clipboard",
          "[session]")
{
    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, 4);
    for (int i = 0; i < 4; ++i)
    {
        session.document.setSample(0, i, static_cast<float>(i + 1), false);
    }
    state.clipboard.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, 1);
    state.clipboard.setSample(0, 0, 99.0f, false);

    session.cursor = 2;
    state.undoables.clear();
    state.redoables.clear();

    cupuacu::actions::audio::performInsertSilence(&state, 2);

    REQUIRE(session.document.getFrameCount() == 6);
    REQUIRE(session.document.getSample(0, 0) == 1.0f);
    REQUIRE(session.document.getSample(0, 1) == 2.0f);
    REQUIRE(session.document.getSample(0, 2) == 0.0f);
    REQUIRE(session.document.getSample(0, 3) == 0.0f);
    REQUIRE(session.document.getSample(0, 4) == 3.0f);
    REQUIRE(state.clipboard.getFrameCount() == 1);
    REQUIRE(state.clipboard.getSample(0, 0) == 99.0f);
}

TEST_CASE("Edit command insert silence replaces the active selection",
          "[session]")
{
    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, 5);
    for (int i = 0; i < 5; ++i)
    {
        session.document.setSample(0, i, static_cast<float>(i + 1), false);
    }

    session.selection.setValue1(1.0);
    session.selection.setValue2(4.0);

    cupuacu::actions::audio::performInsertSilence(&state, 2);

    REQUIRE(session.document.getFrameCount() == 4);
    REQUIRE(session.document.getSample(0, 0) == 1.0f);
    REQUIRE(session.document.getSample(0, 1) == 0.0f);
    REQUIRE(session.document.getSample(0, 2) == 0.0f);
    REQUIRE(session.document.getSample(0, 3) == 5.0f);
}
