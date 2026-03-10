#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "actions/Play.hpp"
#include "actions/Record.hpp"
#include "audio/AudioDevices.hpp"
#include "audio/AudioMessage.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/Window.hpp"

#include <memory>

TEST_CASE("Record action stops playback and records bounded selection",
          "[audio]")
{
    cupuacu::State state{};
    state.audioDevices = std::make_shared<cupuacu::audio::AudioDevices>(false);
    auto &document = state.activeDocumentSession.document;
    document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 128);

    state.audioDevices->applyMessageImmediate(cupuacu::audio::Play{
        .document = &document,
        .startPos = 4,
        .endPos = 20,
        .loopEnabled = false,
        .selectionIsActive = false,
        .selectedChannels = cupuacu::SelectedChannels::BOTH,
        .vuMeter = nullptr});
    REQUIRE(state.audioDevices->isPlaying());

    auto &selection = state.activeDocumentSession.selection;
    selection.setValue1(11.0);
    selection.setValue2(19.0);

    cupuacu::actions::record(&state);
    state.audioDevices->drainQueue();

    REQUIRE(state.audioDevices->isRecording());
    REQUIRE_FALSE(state.audioDevices->isPlaying());
    REQUIRE(state.audioDevices->getRecordingPosition() == 11);
}

TEST_CASE("Device selection short-circuits unchanged values and stores new ones",
          "[audio]")
{
    cupuacu::audio::AudioDevices devices(false);
    const auto initial = devices.getDeviceSelection();

    REQUIRE_FALSE(devices.setDeviceSelection(initial));

    auto changed = initial;
    changed.hostApiIndex = initial.hostApiIndex >= 0 ? -1 : 0;
    changed.outputDeviceIndex = -1;
    changed.inputDeviceIndex = -1;

    REQUIRE(devices.setDeviceSelection(changed));

    const auto stored = devices.getDeviceSelection();
    REQUIRE(stored.hostApiIndex == changed.hostApiIndex);
    REQUIRE(stored.outputDeviceIndex == -1);
    REQUIRE(stored.inputDeviceIndex == -1);
}
