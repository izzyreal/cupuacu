#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestPaths.hpp"
#include "actions/Monitor.hpp"
#include "actions/Play.hpp"
#include "actions/Record.hpp"
#include "audio/AudioDevices.hpp"
#include "audio/AudioMessage.hpp"
#include "audio/MonitorCancellationBackend.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/Window.hpp"

#if defined(__APPLE__)
#include "platform/macos/MicrophonePermission.hpp"
#endif

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace
{
    class TestMonitorCancellationBackend final
        : public cupuacu::audio::MonitorCancellationBackend
    {
    public:
        bool prepare(const uint8_t channels) override
        {
            prepared = channels == 1 || channels == 2;
            return prepared;
        }

        bool process(cupuacu::audio::MonitorProcessingFrame &,
                     const cupuacu::audio::MonitorProcessingFrame &, int, bool,
                     bool,
                     cupuacu::audio::MonitorCancellationMetrics
                         &metrics) noexcept override
        {
            metrics.active = true;
            return prepared;
        }

    private:
        bool prepared = false;
    };
} // namespace

TEST_CASE("Record action stops playback and records bounded selection",
          "[audio]")
{
#if defined(__APPLE__)
    struct MicrophonePermissionReset
    {
        ~MicrophonePermissionReset()
        {
            cupuacu::platform::macos::resetMicrophoneAccessOverrideForTesting();
        }
    } microphonePermissionReset;
    cupuacu::platform::macos::setMicrophoneAccessOverrideForTesting(true);
#endif
    cupuacu::test::StateWithTestPaths state{};
    state.audioDevices = std::make_shared<cupuacu::audio::AudioDevices>(false);
    state.audioDevices->setRecordingPreparationResultForTesting(true);
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

TEST_CASE("Record action reports an unavailable audio input", "[audio]")
{
#if defined(__APPLE__)
    struct MicrophonePermissionReset
    {
        ~MicrophonePermissionReset()
        {
            cupuacu::platform::macos::resetMicrophoneAccessOverrideForTesting();
        }
    } microphonePermissionReset;
    cupuacu::platform::macos::setMicrophoneAccessOverrideForTesting(true);
#endif
    cupuacu::test::StateWithTestPaths state{};
    state.audioDevices = std::make_shared<cupuacu::audio::AudioDevices>(false);
    state.audioDevices->setRecordingPreparationResultForTesting(false);
    std::string reportedTitle;
    std::string reportedMessage;
    state.errorReporter =
        [&](const std::string &title, const std::string &message)
    {
        reportedTitle = title;
        reportedMessage = message;
    };

    cupuacu::actions::record(&state);
    state.audioDevices->drainQueue();

    REQUIRE_FALSE(state.audioDevices->isRecording());
    REQUIRE(reportedTitle == "Audio input unavailable");
#if defined(_WIN32)
    REQUIRE(reportedMessage.find("Allow desktop apps") != std::string::npos);
    REQUIRE(reportedMessage.find("Privacy & security > Microphone") !=
            std::string::npos);
#else
    REQUIRE(reportedMessage.find("Options > Audio") != std::string::npos);
#endif
}

TEST_CASE(
    "Device selection short-circuits unchanged values and stores new ones",
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
    REQUIRE_FALSE(devices.prepareForRecording());
}

TEST_CASE("Input monitoring routes idle input and survives stop", "[audio]")
{
    cupuacu::audio::AudioDevices devices(false);
    REQUIRE(devices.prepareInputMonitorForTesting(
        2, std::make_unique<TestMonitorCancellationBackend>()));
    devices.applyMessageImmediate(cupuacu::audio::SetInputMonitoring{
        .enabled = true, .inputChannelCount = 2, .vuMeter = nullptr});

    std::vector<float> input(256 * 2, 0.2f);
    std::vector<float> output(256 * 2, 0.0f);
    for (int callback = 0; callback < 5; ++callback)
    {
        devices.processCallbackCycle(input.data(), output.data(), 256);
    }

    REQUIRE(std::any_of(output.begin(), output.end(),
                        [](const float sample)
                        {
                            return sample != 0.0f;
                        }));
    REQUIRE(devices.getSnapshot().isInputMonitoringEnabled());

    devices.applyMessageImmediate(cupuacu::audio::Stop{});
    output.assign(output.size(), 0.0f);
    devices.processCallbackCycle(input.data(), output.data(), 256);

    REQUIRE(std::any_of(output.begin(), output.end(),
                        [](const float sample)
                        {
                            return sample != 0.0f;
                        }));
    REQUIRE(devices.getSnapshot().isInputMonitoringEnabled());
}

TEST_CASE("Feedback suppression mode changes reach an active monitor live",
          "[audio]")
{
    cupuacu::audio::AudioDevices devices(false);
    REQUIRE(devices.prepareInputMonitorForTesting(
        2, std::make_unique<TestMonitorCancellationBackend>()));
    devices.applyMessageImmediate(cupuacu::audio::SetInputMonitoring{
        .enabled = true, .inputChannelCount = 2, .vuMeter = nullptr});

    REQUIRE(devices.getFeedbackSuppressionMode() ==
            cupuacu::audio::FeedbackSuppressionMode::Standard);
    REQUIRE(devices.setFeedbackSuppressionMode(
        cupuacu::audio::FeedbackSuppressionMode::Off));
    REQUIRE_FALSE(devices.setFeedbackSuppressionMode(
        cupuacu::audio::FeedbackSuppressionMode::Off));

    std::vector<float> input(256 * 2, 1.0f);
    std::vector<float> output(256 * 2, 0.0f);
    for (int callback = 0; callback < 5; ++callback)
    {
        devices.processCallbackCycle(input.data(), output.data(), 256);
    }

    REQUIRE(devices.getSnapshot().isInputMonitoringEnabled());
    REQUIRE(devices.getMonitorProtectionTelemetry().state ==
            cupuacu::audio::MonitorProtectionState::Disabled);
    REQUIRE(std::any_of(output.begin(), output.end(),
                        [](const float sample)
                        {
                            return sample != 0.0f;
                        }));
}

TEST_CASE("Playback suspends input monitoring and it resumes afterward",
          "[audio]")
{
    cupuacu::audio::AudioDevices devices(false);
    REQUIRE(devices.prepareInputMonitorForTesting(
        2, std::make_unique<TestMonitorCancellationBackend>()));
    cupuacu::Document document{};
    document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 2);
    document.setSample(0, 0, 0.4f, false);
    document.setSample(1, 0, -0.4f, false);
    document.setSample(0, 1, 0.5f, false);
    document.setSample(1, 1, -0.5f, false);

    devices.applyMessageImmediate(cupuacu::audio::SetInputMonitoring{
        .enabled = true, .inputChannelCount = 2, .vuMeter = nullptr});
    devices.applyMessageImmediate(cupuacu::audio::Play{
        .document = &document,
        .startPos = 0,
        .endPos = 2,
        .loopEnabled = false,
        .selectionIsActive = false,
        .selectedChannels = cupuacu::SelectedChannels::BOTH,
        .vuMeter = nullptr});

    const std::vector<float> input{0.9f, -0.9f, 0.8f, -0.8f, 0.7f, -0.7f};
    std::vector<float> output(6, 0.0f);
    devices.processCallbackCycle(input.data(), output.data(), 3);

    REQUIRE(output == std::vector<float>{0.4f, -0.4f, 0.5f, -0.5f, 0.0f, 0.0f});
    REQUIRE_FALSE(devices.isPlaying());

    output.assign(4, 0.0f);
    devices.processCallbackCycle(input.data(), output.data(), 2);
    REQUIRE(output == std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f});
    REQUIRE(devices.getSnapshot().isInputMonitoringEnabled());
    REQUIRE(devices.getMonitorProtectionTelemetry().state ==
            cupuacu::audio::MonitorProtectionState::WarmingUp);
}

TEST_CASE("Monitored recording preserves captured samples", "[audio]")
{
    cupuacu::audio::AudioDevices devices(false);
    REQUIRE(devices.prepareInputMonitorForTesting(
        2, std::make_unique<TestMonitorCancellationBackend>()));
    cupuacu::Document document{};
    document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 8);
    devices.applyMessageImmediate(cupuacu::audio::SetInputMonitoring{
        .enabled = true, .inputChannelCount = 2, .vuMeter = nullptr});
    devices.applyMessageImmediate(cupuacu::audio::Record{.document = &document,
                                                         .startPos = 0,
                                                         .endPos = 0,
                                                         .boundedToEnd = false,
                                                         .vuMeter = nullptr});

    const std::vector<float> input{0.25f, -0.25f, 0.5f, -0.5f};
    std::vector<float> output(4, 0.0f);
    devices.processCallbackCycle(input.data(), output.data(), 2);

    REQUIRE(output == std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f});
    cupuacu::audio::AudioDevices::RecordedChunk chunk{};
    REQUIRE(devices.popRecordedChunk(chunk));
    REQUIRE(chunk.frameCount == 2);
    REQUIRE(chunk.interleavedSamples[0] == input[0]);
    REQUIRE(chunk.interleavedSamples[1] == input[1]);
    REQUIRE(chunk.interleavedSamples[2] == input[2]);
    REQUIRE(chunk.interleavedSamples[3] == input[3]);

    devices.applyMessageImmediate(cupuacu::audio::SetInputMonitoring{
        .enabled = false, .inputChannelCount = 2, .vuMeter = nullptr});
    output.assign(4, 1.0f);
    devices.processCallbackCycle(input.data(), output.data(), 2);
    REQUIRE(output == std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f});

    REQUIRE(devices.popRecordedChunk(chunk));
    REQUIRE(chunk.interleavedSamples[0] == input[0]);
    REQUIRE(chunk.interleavedSamples[1] == input[1]);
}

TEST_CASE("Input monitoring action reports unavailable device selection",
          "[audio]")
{
#if defined(__APPLE__)
    struct MicrophonePermissionReset
    {
        ~MicrophonePermissionReset()
        {
            cupuacu::platform::macos::resetMicrophoneAccessOverrideForTesting();
        }
    } microphonePermissionReset;
    cupuacu::platform::macos::setMicrophoneAccessOverrideForTesting(true);
#endif
    cupuacu::test::StateWithTestPaths state{};
    state.audioDevices = std::make_shared<cupuacu::audio::AudioDevices>(false);
    state.audioDevices->setDeviceSelection(
        {.hostApiIndex = -1, .outputDeviceIndex = -1, .inputDeviceIndex = -1});
    std::string reportedTitle;
    std::string reportedMessage;
    state.errorReporter =
        [&](const std::string &title, const std::string &message)
    {
        reportedTitle = title;
        reportedMessage = message;
    };

    REQUIRE_FALSE(cupuacu::actions::setInputMonitoring(&state, true));
    REQUIRE(reportedTitle == "Input monitoring unavailable");
#if defined(_WIN32)
    REQUIRE(reportedMessage.find("Privacy & security > Microphone") !=
            std::string::npos);
#else
    REQUIRE(reportedMessage.find("Options > Audio") != std::string::npos);
#endif
    REQUIRE_FALSE(state.audioDevices->isInputMonitoringEnabled());
}

#if defined(__APPLE__)
TEST_CASE("Input monitoring remains disabled when microphone access is denied",
          "[audio]")
{
    struct MicrophonePermissionReset
    {
        ~MicrophonePermissionReset()
        {
            cupuacu::platform::macos::resetMicrophoneAccessOverrideForTesting();
        }
    } microphonePermissionReset;
    cupuacu::platform::macos::setMicrophoneAccessOverrideForTesting(false);

    cupuacu::test::StateWithTestPaths state{};
    state.audioDevices = std::make_shared<cupuacu::audio::AudioDevices>(false);
    std::string reportedTitle;
    state.errorReporter = [&](const std::string &title, const std::string &)
    {
        reportedTitle = title;
    };

    REQUIRE_FALSE(cupuacu::actions::setInputMonitoring(&state, true));
    REQUIRE(reportedTitle == "Microphone access required");
    REQUIRE_FALSE(state.audioDevices->isInputMonitoringEnabled());
}
#endif

TEST_CASE(
    "Loop playback update uses new end after pending switch when end is ahead",
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

    devices.processCallbackCycle(nullptr, output.data(),
                                 1); // loop + emit start
    REQUIRE(devices.isPlaying());
    REQUIRE(devices.getPlaybackPosition() == 13);
}

TEST_CASE(
    "Playback update keeps previous end when new non-loop end is behind "
    "current position",
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

TEST_CASE(
    "Playback update uses new non-loop end when it is ahead of current "
    "position",
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

TEST_CASE(
    "Loop playback update keeps old loop until boundary when new end is behind "
    "current position",
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

    devices.processCallbackCycle(nullptr, output.data(),
                                 1); // loop + emit new start
    REQUIRE(devices.isPlaying());
    REQUIRE(devices.getPlaybackPosition() == 13);
}

TEST_CASE(
    "Loop playback update keeps old loop while dragging equivalent update is "
    "deferred",
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
    devices.processCallbackCycle(
        nullptr, output.data(), static_cast<unsigned long>(framesToLoopSample));
    REQUIRE(devices.isPlaying());
    REQUIRE(devices.getPlaybackPosition() == 11);
}

TEST_CASE("Bounded recording stops exactly at the requested end position",
          "[audio]")
{
    cupuacu::audio::AudioDevices devices(false);
    cupuacu::Document document{};
    document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 128);

    devices.applyMessageImmediate(cupuacu::audio::Record{.document = &document,
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
