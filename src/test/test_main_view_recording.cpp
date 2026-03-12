#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestStateBuilders.hpp"
#include "actions/Play.hpp"
#include "actions/Record.hpp"

#include <vector>

TEST_CASE("MainView timerCallback consumes recorded chunks into the document",
          "[gui][audio]")
{
    cupuacu::State state{};
    auto ui = cupuacu::test::createSessionUi(&state, 4, true, 2);
    auto &session = state.activeDocumentSession;
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

    const std::vector<float> input = {
        10.f, -10.f, 11.f, -11.f, 12.f, -12.f};
    state.audioDevices->processCallbackCycle(input.data(), nullptr, 3);

    ui.mainView->timerCallback();

    REQUIRE(doc.getFrameCount() == 7);
    REQUIRE(doc.getSample(0, 4) == Catch::Approx(10.f));
    REQUIRE(doc.getSample(1, 4) == Catch::Approx(-10.f));
    REQUIRE(doc.getSample(0, 6) == Catch::Approx(12.f));
    REQUIRE(doc.getSample(1, 6) == Catch::Approx(-12.f));
    REQUIRE(session.cursor == 7);
    REQUIRE(state.undoables.empty());
}

TEST_CASE("MainView timerCallback finalizes recording into an undoable when stopped",
          "[gui][audio]")
{
    cupuacu::State state{};
    auto ui = cupuacu::test::createSessionUi(&state, 4, true, 2);
    auto &session = state.activeDocumentSession;
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
    REQUIRE(state.undoables.empty());

    cupuacu::actions::requestStop(&state);
    state.audioDevices->drainQueue();
    REQUIRE_FALSE(state.audioDevices->isRecording());

    ui.mainView->timerCallback();

    REQUIRE(state.undoables.size() == 1);
    REQUIRE(state.getUndoDescription() == "Record");

    state.undo();
    REQUIRE(doc.getFrameCount() == 4);
    REQUIRE(doc.getSample(0, 2) == Catch::Approx(2.f));
    REQUIRE(doc.getSample(1, 2) == Catch::Approx(-2.f));
    REQUIRE(doc.getSample(0, 3) == Catch::Approx(3.f));
    REQUIRE(doc.getSample(1, 3) == Catch::Approx(-3.f));
    REQUIRE(session.cursor == 2);

    state.redo();
    REQUIRE(doc.getSample(0, 2) == Catch::Approx(100.f));
    REQUIRE(doc.getSample(1, 2) == Catch::Approx(-100.f));
    REQUIRE(doc.getSample(0, 3) == Catch::Approx(101.f));
    REQUIRE(doc.getSample(1, 3) == Catch::Approx(-101.f));
    REQUIRE(session.cursor == 4);
}
