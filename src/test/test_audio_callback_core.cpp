#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Document.hpp"
#include "audio/AudioCallbackCore.hpp"
#include "audio/MeterAccumulator.hpp"
#include "audio/AudioProcessor.hpp"
#include "audio/RecordedChunk.hpp"
#include "effects/AmplifyFadeEffect.hpp"
#include "effects/DynamicsEffect.hpp"

#include <algorithm>
#include <readerwriterqueue.h>
#include <vector>

namespace
{
    class HalfGainProcessor : public cupuacu::audio::AudioProcessor
    {
    public:
        void process(float *interleavedStereo, const unsigned long frameCount,
                     const cupuacu::audio::AudioProcessContext &) const override
        {
            for (unsigned long i = 0; i < frameCount * 2; ++i)
            {
                interleavedStereo[i] *= 0.5f;
            }
        }
    };

    bool fillOutputBuffer(cupuacu::Document &doc,
                          const bool selectionIsActive,
                          const cupuacu::SelectedChannels selectedChannels,
                          int64_t &playbackPosition,
                          uint64_t &playbackStartPos,
                          uint64_t &playbackEndPos,
                          const bool playbackLoopEnabled,
                          bool &playbackHasPendingSwitch,
                          uint64_t &playbackPendingStartPos,
                          uint64_t &playbackPendingEndPos, bool &isPlaying,
                          float *out, const unsigned long framesPerBuffer,
                          cupuacu::audio::callback_core::StereoMeterLevels &meterLevels,
                          const cupuacu::audio::AudioProcessor *processor = nullptr,
                          const uint64_t effectStartPos = 0,
                          const uint64_t effectEndPos = 0,
                          const cupuacu::SelectedChannels processorChannels =
                              cupuacu::SelectedChannels::BOTH)
    {
        return cupuacu::audio::callback_core::fillOutputBuffer(
            doc.getAudioBuffer(),
            static_cast<uint8_t>(std::clamp<int64_t>(doc.getChannelCount(), 0, 2)),
            selectionIsActive, selectedChannels, playbackPosition,
            playbackStartPos, playbackEndPos, playbackLoopEnabled,
            playbackHasPendingSwitch, playbackPendingStartPos,
            playbackPendingEndPos, isPlaying, out, framesPerBuffer, meterLevels,
            processor, effectStartPos, effectEndPos, processorChannels);
    }
}

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
    cupuacu::audio::callback_core::StereoMeterLevels meterLevels{};
    std::vector<float> out(8, 1.0f);

    const bool playedAnyFrame = fillOutputBuffer(
        doc, false, cupuacu::SelectedChannels::BOTH, playbackPosition,
        playbackStartPos, playbackEndPos, false, playbackHasPendingSwitch,
        playbackPendingStartPos, playbackPendingEndPos, isPlaying, out.data(), 4,
        meterLevels);

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
    cupuacu::audio::callback_core::StereoMeterLevels meterLevels{};
    std::vector<float> out(8, 1.0f); // 4 stereo frames

    const bool playedAnyFrame = fillOutputBuffer(
        doc, false, cupuacu::SelectedChannels::BOTH, playbackPosition,
        playbackStartPos, playbackEndPos, false, playbackHasPendingSwitch,
        playbackPendingStartPos, playbackPendingEndPos, isPlaying, out.data(), 4,
        meterLevels);

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

TEST_CASE("AudioCallbackCore preview processor transforms only played frames",
          "[audio]")
{
    cupuacu::Document doc{};
    doc.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 4);
    doc.setSample(0, 0, 1.0f, false);
    doc.setSample(0, 1, 0.8f, false);
    doc.setSample(0, 2, 0.6f, false);
    doc.setSample(0, 3, 0.4f, false);
    doc.setSample(1, 0, -1.0f, false);
    doc.setSample(1, 1, -0.8f, false);
    doc.setSample(1, 2, -0.6f, false);
    doc.setSample(1, 3, -0.4f, false);

    int64_t playbackPosition = 1;
    uint64_t playbackStartPos = 1;
    uint64_t playbackEndPos = 3;
    bool playbackHasPendingSwitch = false;
    uint64_t playbackPendingStartPos = 0;
    uint64_t playbackPendingEndPos = 0;
    bool isPlaying = true;
    cupuacu::audio::callback_core::StereoMeterLevels meterLevels{};
    std::vector<float> out(8, 0.0f);
    HalfGainProcessor processor{};

    const bool playedAnyFrame = fillOutputBuffer(
        doc, false, cupuacu::SelectedChannels::BOTH, playbackPosition,
        playbackStartPos, playbackEndPos, false, playbackHasPendingSwitch,
        playbackPendingStartPos, playbackPendingEndPos, isPlaying, out.data(), 4,
        meterLevels, &processor, playbackStartPos, playbackEndPos,
        cupuacu::SelectedChannels::BOTH);

    REQUIRE(playedAnyFrame);
    REQUIRE(out[0] == Catch::Approx(0.4f));
    REQUIRE(out[1] == Catch::Approx(-0.4f));
    REQUIRE(out[2] == Catch::Approx(0.3f));
    REQUIRE(out[3] == Catch::Approx(-0.3f));
    REQUIRE(out[4] == 0.0f);
    REQUIRE(out[5] == 0.0f);
    REQUIRE(out[6] == 0.0f);
    REQUIRE(out[7] == 0.0f);
}

TEST_CASE("Amplify/Fade preview processor picks up updated settings between buffers",
          "[audio]")
{
    cupuacu::Document doc{};
    doc.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 4);
    for (int64_t i = 0; i < 4; ++i)
    {
        doc.setSample(0, i, 1.0f, false);
        doc.setSample(1, i, 1.0f, false);
    }

    auto previewSession =
        std::make_shared<cupuacu::effects::AmplifyFadePreviewSession>(
            cupuacu::effects::AmplifyFadeSettings{100.0, 100.0, 0, false});
    auto processor = previewSession->getProcessor();

    int64_t playbackPosition = 0;
    uint64_t playbackStartPos = 0;
    uint64_t playbackEndPos = 2;
    bool playbackHasPendingSwitch = false;
    uint64_t playbackPendingStartPos = 0;
    uint64_t playbackPendingEndPos = 0;
    bool isPlaying = true;
    cupuacu::audio::callback_core::StereoMeterLevels meterLevels{};
    std::vector<float> out(4, 0.0f);

    const bool playedAnyFrame = fillOutputBuffer(
        doc, false, cupuacu::SelectedChannels::BOTH, playbackPosition,
        playbackStartPos, playbackEndPos, false, playbackHasPendingSwitch,
        playbackPendingStartPos, playbackPendingEndPos, isPlaying, out.data(), 2,
        meterLevels, processor.get(), playbackStartPos, playbackEndPos,
        cupuacu::SelectedChannels::BOTH);

    REQUIRE(playedAnyFrame);
    REQUIRE(out[0] == Catch::Approx(1.0f));
    REQUIRE(out[1] == Catch::Approx(1.0f));

    previewSession->updateSettings(
        cupuacu::effects::AmplifyFadeSettings{50.0, 50.0, 0, false});

    playbackPosition = 2;
    playbackStartPos = 2;
    playbackEndPos = 4;
    playbackHasPendingSwitch = false;
    playbackPendingStartPos = 0;
    playbackPendingEndPos = 0;
    isPlaying = true;
    meterLevels = {};
    out.assign(4, 0.0f);

    const bool playedUpdatedFrame =
        fillOutputBuffer(
            doc, false, cupuacu::SelectedChannels::BOTH, playbackPosition,
            playbackStartPos, playbackEndPos, false, playbackHasPendingSwitch,
            playbackPendingStartPos, playbackPendingEndPos, isPlaying,
            out.data(), 2, meterLevels, processor.get(),
            playbackStartPos, playbackEndPos, cupuacu::SelectedChannels::BOTH);

    REQUIRE(playedUpdatedFrame);
    REQUIRE(out[0] == Catch::Approx(0.5f));
    REQUIRE(out[1] == Catch::Approx(0.5f));
}

TEST_CASE("Dynamics preview processor picks up updated settings between buffers",
          "[audio]")
{
    cupuacu::Document doc{};
    doc.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 4);
    for (int64_t i = 0; i < 4; ++i)
    {
        doc.setSample(0, i, 1.0f, false);
        doc.setSample(1, i, 1.0f, false);
    }

    auto previewSession =
        std::make_shared<cupuacu::effects::DynamicsPreviewSession>(
            cupuacu::effects::DynamicsSettings{100.0, 1});
    auto processor = previewSession->getProcessor();

    int64_t playbackPosition = 0;
    uint64_t playbackStartPos = 0;
    uint64_t playbackEndPos = 2;
    bool playbackHasPendingSwitch = false;
    uint64_t playbackPendingStartPos = 0;
    uint64_t playbackPendingEndPos = 0;
    bool isPlaying = true;
    cupuacu::audio::callback_core::StereoMeterLevels meterLevels{};
    std::vector<float> out(4, 0.0f);

    const bool playedAnyFrame = fillOutputBuffer(
        doc, false, cupuacu::SelectedChannels::BOTH, playbackPosition,
        playbackStartPos, playbackEndPos, false, playbackHasPendingSwitch,
        playbackPendingStartPos, playbackPendingEndPos, isPlaying, out.data(), 2,
        meterLevels, processor.get(), playbackStartPos, playbackEndPos,
        cupuacu::SelectedChannels::BOTH);

    REQUIRE(playedAnyFrame);
    REQUIRE(out[0] == Catch::Approx(1.0f));
    REQUIRE(out[1] == Catch::Approx(1.0f));

    previewSession->updateSettings(
        cupuacu::effects::DynamicsSettings{50.0, 1});

    playbackPosition = 2;
    playbackStartPos = 2;
    playbackEndPos = 4;
    playbackHasPendingSwitch = false;
    playbackPendingStartPos = 0;
    playbackPendingEndPos = 0;
    isPlaying = true;
    meterLevels = {};
    out.assign(4, 0.0f);

    const bool playedUpdatedFrame =
        fillOutputBuffer(
            doc, false, cupuacu::SelectedChannels::BOTH, playbackPosition,
            playbackStartPos, playbackEndPos, false, playbackHasPendingSwitch,
            playbackPendingStartPos, playbackPendingEndPos, isPlaying,
            out.data(), 2, meterLevels, processor.get(),
            playbackStartPos, playbackEndPos, cupuacu::SelectedChannels::BOTH);

    REQUIRE(playedUpdatedFrame);
    REQUIRE(out[0] == Catch::Approx(0.625f));
    REQUIRE(out[1] == Catch::Approx(0.625f));
}

TEST_CASE("AudioCallbackCore computes RMS levels for playback output",
          "[audio]")
{
    cupuacu::Document doc{};
    doc.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 2);
    doc.setSample(0, 0, 1.0f, false);
    doc.setSample(0, 1, -1.0f, false);
    doc.setSample(1, 0, 0.5f, false);
    doc.setSample(1, 1, -0.5f, false);

    int64_t playbackPosition = 0;
    uint64_t playbackStartPos = 0;
    uint64_t playbackEndPos = 2;
    bool playbackHasPendingSwitch = false;
    uint64_t playbackPendingStartPos = 0;
    uint64_t playbackPendingEndPos = 0;
    bool isPlaying = true;
    cupuacu::audio::callback_core::StereoMeterLevels meterLevels{};
    std::vector<float> out(4, 0.0f);

    const bool playedAnyFrame = fillOutputBuffer(
        doc, false, cupuacu::SelectedChannels::BOTH, playbackPosition,
        playbackStartPos, playbackEndPos, false, playbackHasPendingSwitch,
        playbackPendingStartPos, playbackPendingEndPos, isPlaying, out.data(), 2,
        meterLevels);

    REQUIRE(playedAnyFrame);
    REQUIRE(meterLevels.peakLeft == Catch::Approx(1.0f));
    REQUIRE(meterLevels.peakRight == Catch::Approx(0.5f));
    REQUIRE(meterLevels.rmsLeft == Catch::Approx(1.0f));
    REQUIRE(meterLevels.rmsRight == Catch::Approx(0.5f));
}

TEST_CASE("AudioCallbackCore computes RMS levels for recorded input",
          "[audio]")
{
    const float input[] = {1.0f, 0.5f, -1.0f, -0.5f};
    int64_t recordingPosition = 0;
    cupuacu::audio::callback_core::StereoMeterLevels meterLevels{};

    moodycamel::ReaderWriterQueue<cupuacu::audio::RecordedChunk> queue(8);
    const auto enqueueChunk =
        [](void *userdata, const cupuacu::audio::RecordedChunk &chunk) -> bool
    {
        auto *typedQueue = static_cast<
            moodycamel::ReaderWriterQueue<cupuacu::audio::RecordedChunk> *>(
            userdata);
        return typedQueue->try_enqueue(chunk);
    };

    cupuacu::audio::callback_core::recordInputIntoChunks(
        input, 2, 2, recordingPosition, &queue, enqueueChunk, meterLevels);

    REQUIRE(recordingPosition == 2);
    REQUIRE(meterLevels.peakLeft == Catch::Approx(1.0f));
    REQUIRE(meterLevels.peakRight == Catch::Approx(0.5f));
    REQUIRE(meterLevels.rmsLeft == Catch::Approx(1.0f));
    REQUIRE(meterLevels.rmsRight == Catch::Approx(0.5f));
}

TEST_CASE("StereoMeterAccumulator computes peak and RMS per channel",
          "[audio]")
{
    cupuacu::audio::StereoMeterAccumulator accumulator{};
    accumulator.addFrame(1.0f, 0.5f);
    accumulator.addFrame(-1.0f, -0.5f);

    cupuacu::audio::callback_core::StereoMeterLevels meterLevels{};
    accumulator.mergeInto(meterLevels);

    REQUIRE(meterLevels.peakLeft == Catch::Approx(1.0f));
    REQUIRE(meterLevels.peakRight == Catch::Approx(0.5f));
    REQUIRE(meterLevels.rmsLeft == Catch::Approx(1.0f));
    REQUIRE(meterLevels.rmsRight == Catch::Approx(0.5f));
}
