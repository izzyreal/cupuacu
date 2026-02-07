#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Document.hpp"
#include "audio/AudioDevices.hpp"
#include "audio/AudioMessage.hpp"
#include "actions/audio/RecordedChunkApplier.hpp"

#include <rtsan_standalone/rtsan_standalone.h>

#include <array>
#include <cstdint>
#include <vector>

TEST_CASE("playback path is safe", "[rtsan]")
{
    __rtsan::Initialize();

    cupuacu::Document doc;
    doc.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 4);
    doc.setSample(0, 0, 0.1f, false);
    doc.setSample(1, 0, -0.1f, false);
    doc.setSample(0, 1, 0.2f, false);
    doc.setSample(1, 1, -0.2f, false);
    doc.setSample(0, 2, 0.3f, false);
    doc.setSample(1, 2, -0.3f, false);
    doc.setSample(0, 3, 0.4f, false);
    doc.setSample(1, 3, -0.4f, false);

    cupuacu::audio::AudioDevices devices(false);
    std::array<float, 8> output{};

    cupuacu::audio::Play playMsg{};
    playMsg.document = &doc;
    playMsg.startPos = 0;
    playMsg.endPos = 4;
    playMsg.loopEnabled = false;
    playMsg.selectionIsActive = false;
    playMsg.selectedChannels = cupuacu::SelectedChannels::BOTH;
    playMsg.vuMeter = nullptr;
    devices.enqueue(playMsg);

    {
        __rtsan::ScopedSanitizeRealtime realtimeScope;
        devices.processCallbackCycle(nullptr, output.data(), 4);
    }

    REQUIRE(output[0] == Catch::Approx(0.1f));
    REQUIRE(output[1] == Catch::Approx(-0.1f));
    REQUIRE(output[6] == Catch::Approx(0.4f));
    REQUIRE(output[7] == Catch::Approx(-0.4f));
}

TEST_CASE("recording overwrite scenario is safe", "[rtsan]")
{
    __rtsan::Initialize();

    cupuacu::Document doc;
    doc.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 8);
    for (int64_t i = 0; i < 8; ++i)
    {
        doc.setSample(0, i, static_cast<float>(i), false);
        doc.setSample(1, i, -static_cast<float>(i), false);
    }

    cupuacu::audio::AudioDevices devices(false);
    int64_t recordingPos = 2; // overwrite existing frames
    const std::vector<float> input = {
        100.f, -100.f, 101.f, -101.f, 102.f, -102.f, 103.f, -103.f};

    cupuacu::audio::Record recordMsg{};
    recordMsg.document = &doc;
    recordMsg.startPos = static_cast<uint64_t>(recordingPos);
    recordMsg.endPos = 0;
    recordMsg.boundedToEnd = false;
    recordMsg.vuMeter = nullptr;
    devices.enqueue(recordMsg);

    {
        __rtsan::ScopedSanitizeRealtime realtimeScope;
        devices.processCallbackCycle(input.data(), nullptr, 4);
    }

    REQUIRE(devices.isRecording());
    REQUIRE(devices.getRecordingPosition() == 6);

    cupuacu::audio::RecordedChunk chunk{};
    REQUIRE(devices.popRecordedChunk(chunk));
    REQUIRE(chunk.channelCount == 2);
    const auto applyResult = cupuacu::actions::audio::applyRecordedChunk(doc, chunk);

    REQUIRE(applyResult.requiredFrameCount == 6);
    REQUIRE(doc.getFrameCount() == 8);
    REQUIRE(doc.getSample(0, 2) == Catch::Approx(100.f));
    REQUIRE(doc.getSample(1, 2) == Catch::Approx(-100.f));
    REQUIRE(doc.getSample(0, 5) == Catch::Approx(103.f));
    REQUIRE(doc.getSample(1, 5) == Catch::Approx(-103.f));
}

TEST_CASE("recording append scenario is safe", "[rtsan]")
{
    __rtsan::Initialize();

    cupuacu::Document doc;
    doc.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 4);
    for (int64_t i = 0; i < 4; ++i)
    {
        doc.setSample(0, i, static_cast<float>(i), false);
        doc.setSample(1, i, -static_cast<float>(i), false);
    }

    cupuacu::audio::AudioDevices devices(false);
    int64_t recordingPos = 4; // append after end
    const std::vector<float> input = {10.f, -10.f, 11.f, -11.f, 12.f, -12.f};

    cupuacu::audio::Record recordMsg{};
    recordMsg.document = &doc;
    recordMsg.startPos = static_cast<uint64_t>(recordingPos);
    recordMsg.endPos = 0;
    recordMsg.boundedToEnd = false;
    recordMsg.vuMeter = nullptr;
    devices.enqueue(recordMsg);

    {
        __rtsan::ScopedSanitizeRealtime realtimeScope;
        devices.processCallbackCycle(input.data(), nullptr, 3);
    }

    REQUIRE(devices.isRecording());
    REQUIRE(devices.getRecordingPosition() == 7);

    cupuacu::audio::RecordedChunk chunk{};
    REQUIRE(devices.popRecordedChunk(chunk));
    REQUIRE(chunk.channelCount == 2);
    const auto applyResult = cupuacu::actions::audio::applyRecordedChunk(doc, chunk);

    REQUIRE(applyResult.requiredFrameCount == 7);
    REQUIRE(doc.getFrameCount() == 7);
    REQUIRE(doc.getSample(0, 4) == Catch::Approx(10.f));
    REQUIRE(doc.getSample(1, 4) == Catch::Approx(-10.f));
    REQUIRE(doc.getSample(0, 6) == Catch::Approx(12.f));
    REQUIRE(doc.getSample(1, 6) == Catch::Approx(-12.f));
}
