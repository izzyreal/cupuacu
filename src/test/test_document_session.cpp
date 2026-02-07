#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "actions/Play.hpp"
#include "audio/AudioDevices.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/DocumentSessionWindow.hpp"
#include "gui/MainView.hpp"

#include <vector>

TEST_CASE("Playback follow does not mutate DocumentSession cursor", "[session]")
{
    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 64);
    session.selection.reset();
    session.cursor = 10;

    state.audioDevices = std::make_shared<cupuacu::audio::AudioDevices>(false);
    state.mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            &state, &session, "test", 800, 400, SDL_WINDOW_HIDDEN);

    cupuacu::gui::MainView mainView(&state);
    state.mainView = &mainView;
    mainView.setBounds(0, 0, 800, 300);

    cupuacu::actions::play(&state);

    std::vector<float> output(8, 0.0f);
    state.audioDevices->processCallbackCycle(nullptr, output.data(), 4);
    mainView.timerCallback();

    REQUIRE(state.audioDevices->isPlaying());
    REQUIRE(state.audioDevices->getPlaybackPosition() == 14);
    REQUIRE(session.cursor == 10);
}
