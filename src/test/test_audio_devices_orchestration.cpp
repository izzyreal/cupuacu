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
    auto &document = state.getActiveDocumentSession().document;
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

    auto &selection = state.getActiveDocumentSession().selection;
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

TEST_CASE("Loop playback update uses new end after pending switch when end is ahead",
          "[audio]")
{
    cupuacu::audio::AudioDevices devices(false);
    cupuacu::Document document{};
    document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 128);

    devices.applyMessageImmediate(cupuacu::audio::Play{
        .document = &document,
        .startPos = 10,
        .endPos = 31,
        .loopEnabled = true,
        .selectionIsActive = true,
        .selectedChannels = cupuacu::SelectedChannels::BOTH,
        .vuMeter = nullptr});

    std::vector<float> output(64, 0.0f);

    devices.processCallbackCycle(nullptr, output.data(), 12); // pos 22
    REQUIRE(devices.getPlaybackPosition() == 22);

    devices.applyMessageImmediate(cupuacu::audio::UpdatePlayback{
        .startPos = 12,
        .endPos = 27,
        .loopEnabled = true,
        .selectionIsActive = true,
        .selectedChannels = cupuacu::SelectedChannels::BOTH});

    devices.processCallbackCycle(nullptr, output.data(), 4); // pos 26
    REQUIRE(devices.getPlaybackPosition() == 26);

    devices.processCallbackCycle(nullptr, output.data(), 1); // hit new end
    REQUIRE(devices.getPlaybackPosition() == 27);

    devices.processCallbackCycle(nullptr, output.data(), 1); // loop + emit start
    REQUIRE(devices.isPlaying());
    REQUIRE(devices.getPlaybackPosition() == 13);
}

TEST_CASE("Playback update keeps previous end when new non-loop end is behind current position",
          "[audio]")
{
    cupuacu::audio::AudioDevices devices(false);
    cupuacu::Document document{};
    document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 128);

    devices.applyMessageImmediate(cupuacu::audio::Play{
        .document = &document,
        .startPos = 10,
        .endPos = 31,
        .loopEnabled = false,
        .selectionIsActive = true,
        .selectedChannels = cupuacu::SelectedChannels::BOTH,
        .vuMeter = nullptr});

    std::vector<float> output(64, 0.0f);

    devices.processCallbackCycle(nullptr, output.data(), 4); // pos 14
    REQUIRE(devices.getPlaybackPosition() == 14);

    devices.processCallbackCycle(nullptr, output.data(), 8); // pos 22
    REQUIRE(devices.isPlaying());
    REQUIRE(devices.getPlaybackPosition() == 22);

    devices.applyMessageImmediate(cupuacu::audio::UpdatePlayback{
        .startPos = 12,
        .endPos = 21,
        .loopEnabled = false,
        .selectionIsActive = true,
        .selectedChannels = cupuacu::SelectedChannels::BOTH});

    devices.processCallbackCycle(nullptr, output.data(), 1);
    REQUIRE(devices.isPlaying());

    devices.processCallbackCycle(nullptr, output.data(), 8);
    REQUIRE(devices.isPlaying());

    devices.processCallbackCycle(nullptr, output.data(), 1);
    REQUIRE_FALSE(devices.isPlaying());
}

TEST_CASE("Playback update uses new non-loop end when it is ahead of current position",
          "[audio]")
{
    cupuacu::audio::AudioDevices devices(false);
    cupuacu::Document document{};
    document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 128);

    devices.applyMessageImmediate(cupuacu::audio::Play{
        .document = &document,
        .startPos = 10,
        .endPos = 31,
        .loopEnabled = false,
        .selectionIsActive = true,
        .selectedChannels = cupuacu::SelectedChannels::BOTH,
        .vuMeter = nullptr});

    std::vector<float> output(64, 0.0f);

    devices.processCallbackCycle(nullptr, output.data(), 4); // pos 14
    REQUIRE(devices.getPlaybackPosition() == 14);

    devices.applyMessageImmediate(cupuacu::audio::UpdatePlayback{
        .startPos = 12,
        .endPos = 26,
        .loopEnabled = false,
        .selectionIsActive = true,
        .selectedChannels = cupuacu::SelectedChannels::BOTH});

    devices.processCallbackCycle(nullptr, output.data(), 12); // pos 26
    REQUIRE(devices.isPlaying());

    devices.processCallbackCycle(nullptr, output.data(), 1);
    REQUIRE_FALSE(devices.isPlaying());
}

TEST_CASE("Loop playback update keeps old loop until boundary when new end is behind current position",
          "[audio]")
{
    cupuacu::audio::AudioDevices devices(false);
    cupuacu::Document document{};
    document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 128);

    devices.applyMessageImmediate(cupuacu::audio::Play{
        .document = &document,
        .startPos = 10,
        .endPos = 31,
        .loopEnabled = true,
        .selectionIsActive = true,
        .selectedChannels = cupuacu::SelectedChannels::BOTH,
        .vuMeter = nullptr});

    std::vector<float> output(64, 0.0f);

    devices.processCallbackCycle(nullptr, output.data(), 12); // pos 22
    REQUIRE(devices.getPlaybackPosition() == 22);

    devices.applyMessageImmediate(cupuacu::audio::UpdatePlayback{
        .startPos = 12,
        .endPos = 21,
        .loopEnabled = true,
        .selectionIsActive = true,
        .selectedChannels = cupuacu::SelectedChannels::BOTH});

    devices.processCallbackCycle(nullptr, output.data(), 8); // pos 30
    REQUIRE(devices.getPlaybackPosition() == 30);

    devices.processCallbackCycle(nullptr, output.data(), 1); // hit old end
    REQUIRE(devices.getPlaybackPosition() == 31);

    devices.processCallbackCycle(nullptr, output.data(), 1); // loop + emit new start
    REQUIRE(devices.isPlaying());
    REQUIRE(devices.getPlaybackPosition() == 13);
}

TEST_CASE("Loop playback update keeps old loop while dragging equivalent update is deferred",
          "[audio]")
{
    cupuacu::audio::AudioDevices devices(false);
    cupuacu::Document document{};
    document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 128);

    devices.applyMessageImmediate(cupuacu::audio::Play{
        .document = &document,
        .startPos = 10,
        .endPos = 31,
        .loopEnabled = true,
        .selectionIsActive = true,
        .selectedChannels = cupuacu::SelectedChannels::BOTH,
        .vuMeter = nullptr});

    std::vector<float> output(64, 0.0f);

    devices.processCallbackCycle(nullptr, output.data(), 4); // pos 14
    REQUIRE(devices.getPlaybackPosition() == 14);

    // Equivalent to the UI deferring the update until drag release:
    // no UpdatePlayback is sent while dragging, so old range remains active.
    const int64_t framesToLoopSample = 18;
    devices.processCallbackCycle(nullptr, output.data(),
                                 static_cast<unsigned long>(framesToLoopSample));
    REQUIRE(devices.isPlaying());
    REQUIRE(devices.getPlaybackPosition() == 11);
}

TEST_CASE("Bounded recording stops exactly at the requested end position",
          "[audio]")
{
    cupuacu::audio::AudioDevices devices(false);
    cupuacu::Document document{};
    document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 128);

    devices.applyMessageImmediate(cupuacu::audio::Record{
        .document = &document,
        .startPos = 5,
        .endPos = 9,
        .boundedToEnd = true,
        .vuMeter = nullptr});

    std::vector<float> input(2 * 16, 0.25f);
    std::vector<float> output(2 * 16, 0.0f);

    devices.processCallbackCycle(input.data(), output.data(), 16);

    REQUIRE_FALSE(devices.isRecording());
    REQUIRE(devices.getRecordingPosition() == 9);

    cupuacu::audio::AudioDevices::RecordedChunk chunk{};
    REQUIRE(devices.popRecordedChunk(chunk));
    REQUIRE(chunk.startFrame == 5);
    REQUIRE(chunk.frameCount == 4);
}
