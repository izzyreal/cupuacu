#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Document.hpp"
#include "SelectedChannels.hpp"
#include "TestPaths.hpp"
#include "actions/effects/BackgroundEffect.hpp"
#include "audio/AudioCallbackCore.hpp"
#include "effects/AmplifyEnvelopeEffect.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/Window.hpp"

#include <algorithm>
#include <vector>

namespace
{
    void drainPendingEffectWork(cupuacu::State *state)
    {
        for (int attempt = 0; attempt < 5000; ++attempt)
        {
            cupuacu::actions::effects::processPendingEffectWork(state);
            if (!state->backgroundEffectJob)
            {
                return;
            }
        }

        FAIL("Timed out waiting for background amplify envelope work");
    }

    void initializeMonoDocument(cupuacu::State &state,
                                const std::vector<float> &samples)
    {
        auto &document = state.getActiveDocumentSession().document;
        document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1,
                            static_cast<int64_t>(samples.size()));
        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            document.setSample(0, static_cast<int64_t>(i), samples[i], false);
        }
    }

    std::vector<float> readMonoSamples(const cupuacu::Document &document)
    {
        std::vector<float> result(
            static_cast<std::size_t>(document.getFrameCount()));
        for (int64_t i = 0; i < document.getFrameCount(); ++i)
        {
            result[static_cast<std::size_t>(i)] = document.getSample(0, i);
        }
        return result;
    }
} // namespace

TEST_CASE("Amplify Envelope applies linear envelope points and supports undo",
          "[effects]")
{
    cupuacu::test::StateWithTestPaths state{};
    initializeMonoDocument(state, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f});

    cupuacu::effects::AmplifyEnvelopeSettings settings{};
    settings.points = {{0.0, 100.0}, {0.5, 0.0}, {1.0, 100.0}};
    cupuacu::effects::performAmplifyEnvelope(&state, settings);
    drainPendingEffectWork(&state);

    const auto processed =
        readMonoSamples(state.getActiveDocumentSession().document);
    REQUIRE(processed[0] == Catch::Approx(1.0f));
    REQUIRE(processed[1] == Catch::Approx(0.5f));
    REQUIRE(processed[2] == Catch::Approx(0.0f));
    REQUIRE(processed[3] == Catch::Approx(0.5f));
    REQUIRE(processed[4] == Catch::Approx(1.0f));

    state.undo();
    REQUIRE(readMonoSamples(state.getActiveDocumentSession().document) ==
            std::vector<float>({1.0f, 1.0f, 1.0f, 1.0f, 1.0f}));
}

TEST_CASE("Amplify Envelope runs in the background and commits undoably",
          "[effects]")
{
    cupuacu::test::StateWithTestPaths state{};
    initializeMonoDocument(state, {1.0f, 1.0f, 1.0f, 1.0f});

    cupuacu::effects::AmplifyEnvelopeSettings settings{};
    settings.points = {{0.0, 100.0}, {1.0, 50.0}};
    cupuacu::effects::performAmplifyEnvelope(&state, settings);

    REQUIRE(state.backgroundEffectJob != nullptr);
    REQUIRE(state.longTask.active);
    REQUIRE(state.longTask.title == "Applying effect");
    REQUIRE(state.longTask.detail == "Amplify Envelope");

    drainPendingEffectWork(&state);

    const auto processed =
        readMonoSamples(state.getActiveDocumentSession().document);
    REQUIRE(processed[0] == Catch::Approx(1.0f));
    REQUIRE(processed[1] == Catch::Approx(5.0f / 6.0f));
    REQUIRE(processed[2] == Catch::Approx(2.0f / 3.0f));
    REQUIRE(processed[3] == Catch::Approx(0.5f));

    state.undo();
    REQUIRE(readMonoSamples(state.getActiveDocumentSession().document) ==
            std::vector<float>({1.0f, 1.0f, 1.0f, 1.0f}));
}

TEST_CASE("Amplify Envelope normalize resets to a flat normalized envelope",
          "[effects]")
{
    cupuacu::test::StateWithTestPaths state{};
    initializeMonoDocument(state, {0.25f, -0.5f, 0.1f});

    cupuacu::effects::AmplifyEnvelopeSettings settings{};
    settings.points = {{0.0, 50.0}, {0.5, 20.0}, {1.0, 100.0}};
    settings.fadeLengthMs = 250.0;
    settings.snapEnabled = true;
    cupuacu::effects::normalizeAmplifyEnvelopeSettings(settings, &state);

    REQUIRE(settings.points.size() == 2);
    REQUIRE(settings.points[0].percent == Catch::Approx(200.0));
    REQUIRE(settings.points[1].position == Catch::Approx(1.0));
    REQUIRE(settings.points[1].percent == Catch::Approx(200.0));
    REQUIRE(settings.fadeLengthMs == Catch::Approx(250.0));
    REQUIRE(settings.snapEnabled);
}

TEST_CASE(
    "Amplify Envelope fade in and out action configures envelope from fade "
    "length",
    "[effects]")
{
    cupuacu::test::StateWithTestPaths state{};
    initializeMonoDocument(state, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f});

    auto &document = state.getActiveDocumentSession().document;
    document.initialize(cupuacu::SampleFormat::FLOAT32, 1000, 1, 1000);
    state.getActiveDocumentSession().selection.setHighest(1000.0);
    state.getActiveDocumentSession().selection.setValue1(0.0);
    state.getActiveDocumentSession().selection.setValue2(1000.0);

    cupuacu::effects::AmplifyEnvelopeSettings settings{};
    settings.fadeLengthMs = 250.0;
    settings.snapEnabled = true;
    cupuacu::effects::configureAmplifyEnvelopeFadeInOut(settings, &state);

    REQUIRE(settings.points.size() == 4);
    REQUIRE(settings.points[0].position == Catch::Approx(0.0));
    REQUIRE(settings.points[0].percent == Catch::Approx(0.0));
    REQUIRE(settings.points[1].position == Catch::Approx(0.25));
    REQUIRE(settings.points[1].percent == Catch::Approx(100.0));
    REQUIRE(settings.points[2].position == Catch::Approx(0.75));
    REQUIRE(settings.points[2].percent == Catch::Approx(100.0));
    REQUIRE(settings.points[3].position == Catch::Approx(1.0));
    REQUIRE(settings.points[3].percent == Catch::Approx(0.0));
    REQUIRE(settings.fadeLengthMs == Catch::Approx(250.0));
    REQUIRE(settings.snapEnabled);
}

TEST_CASE(
    "Amplify Envelope preview processor picks up updated settings between "
    "buffers",
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
        std::make_shared<cupuacu::effects::AmplifyEnvelopePreviewSession>(
            cupuacu::effects::AmplifyEnvelopeSettings{});
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

    const bool playedAnyFrame = cupuacu::audio::callback_core::fillOutputBuffer(
        doc.getAudioBuffer(),
        static_cast<uint8_t>(std::clamp<int64_t>(doc.getChannelCount(), 0, 2)),
        false, cupuacu::SelectedChannels::BOTH, playbackPosition,
        playbackStartPos, playbackEndPos, false, playbackHasPendingSwitch,
        playbackPendingStartPos, playbackPendingEndPos, isPlaying, out.data(),
        2, meterLevels, processor.get(), playbackStartPos, playbackEndPos,
        cupuacu::SelectedChannels::BOTH);

    REQUIRE(playedAnyFrame);
    REQUIRE(out[0] == Catch::Approx(1.0f));
    REQUIRE(out[1] == Catch::Approx(1.0f));

    cupuacu::effects::AmplifyEnvelopeSettings updated{};
    updated.points = {{0.0, 50.0}, {1.0, 50.0}};
    previewSession->updateSettings(updated);

    playbackPosition = 2;
    playbackStartPos = 2;
    playbackEndPos = 4;
    meterLevels = {};
    out.assign(4, 0.0f);

    const bool playedUpdatedFrame =
        cupuacu::audio::callback_core::fillOutputBuffer(
            doc.getAudioBuffer(),
            static_cast<uint8_t>(std::clamp<int64_t>(doc.getChannelCount(), 0, 2)),
            false, cupuacu::SelectedChannels::BOTH, playbackPosition,
            playbackStartPos, playbackEndPos, false, playbackHasPendingSwitch,
            playbackPendingStartPos, playbackPendingEndPos, isPlaying,
            out.data(), 2, meterLevels, processor.get(), playbackStartPos,
            playbackEndPos, cupuacu::SelectedChannels::BOTH);

    REQUIRE(playedUpdatedFrame);
    REQUIRE(out[0] == Catch::Approx(0.5f));
    REQUIRE(out[1] == Catch::Approx(0.5f));
}
