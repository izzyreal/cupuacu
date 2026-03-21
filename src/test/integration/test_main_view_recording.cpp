#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "IntegrationTestHelpers.hpp"

#include "State.hpp"
#include "actions/Play.hpp"
#include "actions/Record.hpp"
#include "gui/DevicePropertiesWindow.hpp"

#include <vector>

using Catch::Approx;

TEST_CASE("MainView integration consumes recorded chunks into the document",
          "[integration]")
{
    cupuacu::State state{};
    auto ui = cupuacu::test::integration::createSessionUi(&state, 4, true, 2);
    auto &session = state.getActiveDocumentSession();
    auto &doc = session.document;

    for (int64_t i = 0; i < 4; ++i)
    {
        doc.setSample(0, i, static_cast<float>(i), false);
        doc.setSample(1, i, -static_cast<float>(i), false);
    }

    session.cursor = 4;
    cupuacu::actions::record(&state);
    state.audioDevices->drainQueue();
    REQUIRE(state.audioDevices->isRecording());

    const std::vector<float> input = {10.f, -10.f, 11.f, -11.f, 12.f, -12.f};
    state.audioDevices->processCallbackCycle(input.data(), nullptr, 3);

    ui.mainView->timerCallback();

    REQUIRE(doc.getFrameCount() == 7);
    REQUIRE(doc.getSample(0, 4) == Approx(10.f));
    REQUIRE(doc.getSample(1, 4) == Approx(-10.f));
    REQUIRE(doc.getSample(0, 6) == Approx(12.f));
    REQUIRE(doc.getSample(1, 6) == Approx(-12.f));
    REQUIRE(session.cursor == 7);
    REQUIRE(state.getActiveUndoables().empty());
}

TEST_CASE("MainView integration finalizes recording into an undoable when stopped",
          "[integration]")
{
    cupuacu::State state{};
    auto ui = cupuacu::test::integration::createSessionUi(&state, 4, true, 2);
    auto &session = state.getActiveDocumentSession();
    auto &doc = session.document;

    for (int64_t i = 0; i < 4; ++i)
    {
        doc.setSample(0, i, static_cast<float>(i), false);
        doc.setSample(1, i, -static_cast<float>(i), false);
    }

    session.cursor = 2;
    cupuacu::actions::record(&state);
    state.audioDevices->drainQueue();
    REQUIRE(state.audioDevices->isRecording());

    const std::vector<float> input = {100.f, -100.f, 101.f, -101.f};
    state.audioDevices->processCallbackCycle(input.data(), nullptr, 2);
    ui.mainView->timerCallback();
    REQUIRE(state.getActiveUndoables().empty());

    cupuacu::actions::requestStop(&state);
    state.audioDevices->drainQueue();
    REQUIRE_FALSE(state.audioDevices->isRecording());

    ui.mainView->timerCallback();

    REQUIRE(state.getActiveUndoables().size() == 1);
    REQUIRE(state.getUndoDescription() == "Record");

    state.undo();
    REQUIRE(doc.getFrameCount() == 4);
    REQUIRE(doc.getSample(0, 2) == Approx(2.f));
    REQUIRE(doc.getSample(1, 2) == Approx(-2.f));
    REQUIRE(doc.getSample(0, 3) == Approx(3.f));
    REQUIRE(doc.getSample(1, 3) == Approx(-3.f));
    REQUIRE(session.cursor == 2);

    state.redo();
    REQUIRE(doc.getSample(0, 2) == Approx(100.f));
    REQUIRE(doc.getSample(1, 2) == Approx(-100.f));
    REQUIRE(doc.getSample(0, 3) == Approx(101.f));
    REQUIRE(doc.getSample(1, 3) == Approx(-101.f));
    REQUIRE(session.cursor == 4);
}
