#include <catch2/catch_test_macros.hpp>

#include "IntegrationTestHelpers.hpp"

#include "State.hpp"
#include "actions/Play.hpp"
#include "gui/DevicePropertiesWindow.hpp"

#include <vector>

TEST_CASE("Playback follow integration does not mutate DocumentSession cursor",
          "[integration]")
{
    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    auto ui = cupuacu::test::integration::createSessionUi(&state, 64, true);
    session.selection.reset();
    session.cursor = 10;

    cupuacu::actions::play(&state);

    std::vector<float> output(8, 0.0f);
    state.audioDevices->processCallbackCycle(nullptr, output.data(), 4);
    ui.mainView->timerCallback();

    REQUIRE(state.audioDevices->isPlaying());
    REQUIRE(state.audioDevices->getPlaybackPosition() == 14);
    REQUIRE(session.cursor == 10);
}
