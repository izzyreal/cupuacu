#include <catch2/catch_test_macros.hpp>

#include "Document.hpp"
#include "audio/AudioCallbackCore.hpp"

#include <vector>

TEST_CASE("AudioCallbackCore fillOutputBuffer handles invalid negative playback position",
          "[audio]")
{
    cupuacu::Document doc{};
    doc.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 4);
    for (int64_t i = 0; i < 4; ++i)
    {
        doc.setSample(0, i, 0.25f, false);
        doc.setSample(1, i, -0.25f, false);
    }

    int64_t playbackPosition = -1;
    uint64_t playbackStartPos = 0;
    uint64_t playbackEndPos = 4;
    bool playbackHasPendingSwitch = false;
    uint64_t playbackPendingStartPos = 0;
    uint64_t playbackPendingEndPos = 0;
    bool isPlaying = true;
    float peakLeft = 0.0f;
    float peakRight = 0.0f;
    std::vector<float> out(8, 1.0f);

    const bool playedAnyFrame = cupuacu::audio::callback_core::fillOutputBuffer(
        &doc, false, cupuacu::SelectedChannels::BOTH, playbackPosition,
        playbackStartPos, playbackEndPos, false, playbackHasPendingSwitch,
        playbackPendingStartPos, playbackPendingEndPos, isPlaying, out.data(), 4,
        peakLeft, peakRight);

    REQUIRE_FALSE(playedAnyFrame);
    REQUIRE(playbackPosition == -1);
    REQUIRE(isPlaying);
    for (float sample : out)
    {
        REQUIRE(sample == 0.0f);
    }
}

TEST_CASE("AudioCallbackCore writes silence after non-loop stop in buffer",
          "[audio]")
{
    cupuacu::Document doc{};
    doc.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 4);
    doc.setSample(0, 0, 0.10f, false);
    doc.setSample(0, 1, 0.20f, false);
    doc.setSample(0, 2, 0.30f, false);
    doc.setSample(0, 3, 0.40f, false);
    doc.setSample(1, 0, -0.10f, false);
    doc.setSample(1, 1, -0.20f, false);
    doc.setSample(1, 2, -0.30f, false);
    doc.setSample(1, 3, -0.40f, false);

    int64_t playbackPosition = 2;
    uint64_t playbackStartPos = 2;
    uint64_t playbackEndPos = 3; // only frame 2 should play
    bool playbackHasPendingSwitch = false;
    uint64_t playbackPendingStartPos = 0;
    uint64_t playbackPendingEndPos = 0;
    bool isPlaying = true;
    float peakLeft = 0.0f;
    float peakRight = 0.0f;
    std::vector<float> out(8, 1.0f); // 4 stereo frames

    const bool playedAnyFrame = cupuacu::audio::callback_core::fillOutputBuffer(
        &doc, false, cupuacu::SelectedChannels::BOTH, playbackPosition,
        playbackStartPos, playbackEndPos, false, playbackHasPendingSwitch,
        playbackPendingStartPos, playbackPendingEndPos, isPlaying, out.data(), 4,
        peakLeft, peakRight);

    REQUIRE(playedAnyFrame);
    REQUIRE_FALSE(isPlaying);
    REQUIRE(playbackPosition == -1);

    // Frame 0: valid sample at index 2.
    REQUIRE(out[0] == 0.30f);
    REQUIRE(out[1] == -0.30f);
    // Remaining frames should be silence.
    for (std::size_t i = 2; i < out.size(); ++i)
    {
        REQUIRE(out[i] == 0.0f);
    }
}
