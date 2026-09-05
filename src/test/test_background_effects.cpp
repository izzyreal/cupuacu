#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "BackgroundEffectTestUtil.hpp"
#include "State.hpp"
#include "TestPaths.hpp"
#include "actions/effects/BackgroundEffect.hpp"
#include "effects/AmplifyFadeEffect.hpp"
#include "effects/AmplifyEnvelopeEffect.hpp"
#include "effects/DynamicsEffect.hpp"
#include "effects/RemoveSilenceEffect.hpp"
#include "effects/ReverseEffect.hpp"

TEST_CASE("Reverse effect runs in the background and commits undoably",
          "[effects]")
{
    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();
    auto &document = session.document;
    document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, 4);
    document.setSample(0, 0, 0.1f, false);
    document.setSample(0, 1, 0.2f, false);
    document.setSample(0, 2, 0.3f, false);
    document.setSample(0, 3, 0.4f, false);
    const auto originalBuffer = document.getAudioBuffer();

    cupuacu::effects::performReverse(&state);

    REQUIRE(state.backgroundEffectJob != nullptr);
    REQUIRE(state.longTask.active);
    REQUIRE(state.longTask.title == "Applying effect");
    REQUIRE(state.longTask.detail == "Reverse");
    REQUIRE_FALSE(state.canUndo());

    cupuacu::test::drainPendingEffectWork(&state);

    REQUIRE(state.backgroundEffectJob == nullptr);
    REQUIRE_FALSE(state.longTask.active);
    REQUIRE(state.canUndo());
    REQUIRE(state.getUndoDescription() == "Reverse");
    REQUIRE(document.getSample(0, 0) == Catch::Approx(0.4f));
    REQUIRE(document.getSample(0, 1) == Catch::Approx(0.3f));
    REQUIRE(document.getSample(0, 2) == Catch::Approx(0.2f));
    REQUIRE(document.getSample(0, 3) == Catch::Approx(0.1f));
    const auto effectedBuffer = document.getAudioBuffer();
    REQUIRE(effectedBuffer != originalBuffer);

    state.undo();
    REQUIRE(document.getAudioBuffer() == originalBuffer);
    REQUIRE(document.getSample(0, 0) == Catch::Approx(0.1f));
    REQUIRE(document.getSample(0, 1) == Catch::Approx(0.2f));
    REQUIRE(document.getSample(0, 2) == Catch::Approx(0.3f));
    REQUIRE(document.getSample(0, 3) == Catch::Approx(0.4f));

    state.redo();
    REQUIRE(document.getAudioBuffer() == effectedBuffer);
    REQUIRE(document.getSample(0, 0) == Catch::Approx(0.4f));
    REQUIRE(document.getSample(0, 1) == Catch::Approx(0.3f));
    REQUIRE(document.getSample(0, 2) == Catch::Approx(0.2f));
    REQUIRE(document.getSample(0, 3) == Catch::Approx(0.1f));
}

TEST_CASE("Amplify/Fade runs in the background and commits undoably",
          "[effects]")
{
    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();
    auto &document = session.document;
    document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, 4);
    document.setSample(0, 0, 1.0f, false);
    document.setSample(0, 1, 1.0f, false);
    document.setSample(0, 2, 1.0f, false);
    document.setSample(0, 3, 1.0f, false);

    cupuacu::effects::performAmplifyFade(
        &state, cupuacu::effects::AmplifyFadeSettings{100.0, 200.0, 0, false});

    REQUIRE(state.backgroundEffectJob != nullptr);
    REQUIRE(state.longTask.active);
    REQUIRE(state.longTask.title == "Applying effect");
    REQUIRE(state.longTask.detail == "Amplify/Fade");
    REQUIRE_FALSE(state.canUndo());

    cupuacu::test::drainPendingEffectWork(&state);

    REQUIRE(state.backgroundEffectJob == nullptr);
    REQUIRE_FALSE(state.longTask.active);
    REQUIRE(state.canUndo());
    REQUIRE(state.getUndoDescription() == "Amplify/Fade");
    REQUIRE(document.getSample(0, 0) == Catch::Approx(1.0f));
    REQUIRE(document.getSample(0, 1) == Catch::Approx(1.3333333f));
    REQUIRE(document.getSample(0, 2) == Catch::Approx(1.6666666f));
    REQUIRE(document.getSample(0, 3) == Catch::Approx(2.0f));

    state.undo();
    REQUIRE(document.getSample(0, 0) == Catch::Approx(1.0f));
    REQUIRE(document.getSample(0, 1) == Catch::Approx(1.0f));
    REQUIRE(document.getSample(0, 2) == Catch::Approx(1.0f));
    REQUIRE(document.getSample(0, 3) == Catch::Approx(1.0f));
}

TEST_CASE("Dynamics runs in the background and commits undoably", "[effects]")
{
    cupuacu::test::StateWithTestPaths state{};
    auto &document = state.getActiveDocumentSession().document;
    document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, 4);
    document.setSample(0, 0, 0.2f, false);
    document.setSample(0, 1, 0.5f, false);
    document.setSample(0, 2, 0.9f, false);
    document.setSample(0, 3, -1.0f, false);

    cupuacu::effects::performDynamics(
        &state, cupuacu::effects::DynamicsSettings{50.0, 1});

    REQUIRE(state.backgroundEffectJob != nullptr);
    REQUIRE(state.longTask.active);
    REQUIRE(state.longTask.title == "Applying effect");
    REQUIRE(state.longTask.detail == "Dynamics");
    REQUIRE_FALSE(state.canUndo());

    cupuacu::test::drainPendingEffectWork(&state);

    REQUIRE(state.backgroundEffectJob == nullptr);
    REQUIRE_FALSE(state.longTask.active);
    REQUIRE(state.canUndo());
    REQUIRE(state.getUndoDescription() == "Dynamics");
    REQUIRE(document.getSample(0, 0) == Catch::Approx(0.2f));
    REQUIRE(document.getSample(0, 1) == Catch::Approx(0.5f));
    REQUIRE(document.getSample(0, 2) == Catch::Approx(0.6f));
    REQUIRE(document.getSample(0, 3) == Catch::Approx(-0.625f));

    state.undo();
    REQUIRE(document.getSample(0, 0) == Catch::Approx(0.2f));
    REQUIRE(document.getSample(0, 1) == Catch::Approx(0.5f));
    REQUIRE(document.getSample(0, 2) == Catch::Approx(0.9f));
    REQUIRE(document.getSample(0, 3) == Catch::Approx(-1.0f));
}

TEST_CASE("Remove silence runs in the background and commits undoably",
          "[effects]")
{
    cupuacu::test::StateWithTestPaths state{};
    auto &document = state.getActiveDocumentSession().document;
    document.initialize(cupuacu::SampleFormat::FLOAT32, 100, 1, 8);
    document.setSample(0, 0, 0.5f, false);
    document.setSample(0, 3, 0.4f, false);
    document.setSample(0, 7, 0.3f, false);

    cupuacu::effects::RemoveSilenceSettings settings{};
    settings.modeIndex = 1;
    settings.thresholdUnitIndex = 1;
    settings.thresholdSampleValue = 0.01;
    settings.minimumSilenceLengthMs = 10.0;
    cupuacu::effects::performRemoveSilence(&state, settings);

    REQUIRE(state.backgroundEffectJob != nullptr);
    REQUIRE(state.longTask.active);
    REQUIRE(state.longTask.title == "Applying effect");
    REQUIRE(state.longTask.detail == "Remove silence");
    REQUIRE_FALSE(state.canUndo());

    cupuacu::test::drainPendingEffectWork(&state);

    REQUIRE(state.backgroundEffectJob == nullptr);
    REQUIRE_FALSE(state.longTask.active);
    REQUIRE(state.canUndo());
    REQUIRE(state.getUndoDescription() == "Remove silence");
    REQUIRE(document.getFrameCount() == 3);
    REQUIRE(document.getSample(0, 0) == Catch::Approx(0.5f));
    REQUIRE(document.getSample(0, 1) == Catch::Approx(0.4f));
    REQUIRE(document.getSample(0, 2) == Catch::Approx(0.3f));

    state.undo();
    REQUIRE(document.getFrameCount() == 8);
    REQUIRE(document.getSample(0, 0) == Catch::Approx(0.5f));
    REQUIRE(document.getSample(0, 3) == Catch::Approx(0.4f));
    REQUIRE(document.getSample(0, 7) == Catch::Approx(0.3f));
}

namespace
{
    void requireMatchingPeaks(cupuacu::DocumentSession &session)
    {
        session.rebuildWaveformCacheSynchronously();
        cupuacu::waveform::DocumentWaveformCaches reference;
        reference.rebuildSynchronously(session.document);
        for (int channel = 0; channel < session.document.getChannelCount();
             ++channel)
        {
            const auto &actual = session.getWaveformCache(channel);
            const auto &expected = reference.getCache(channel);
            REQUIRE_FALSE(actual.hasDirtyBlocks());
            REQUIRE(actual.levelsCount() == expected.levelsCount());
            for (int level = 0; level < expected.levelsCount(); ++level)
            {
                const auto &a = actual.getLevelByIndex(level);
                const auto &b = expected.getLevelByIndex(level);
                REQUIRE(a.size() == b.size());
                for (std::size_t peak = 0; peak < a.size(); ++peak)
                {
                    REQUIRE(a[peak].min == b[peak].min);
                    REQUIRE(a[peak].max == b[peak].max);
                }
            }
        }
    }
} // namespace

TEST_CASE("Partial background effects reuse peaks across undo and redo",
          "[effects][waveform]")
{
    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();
    auto &document = session.document;
    document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 1031);
    for (int64_t frame = 0; frame < 1031; ++frame)
    {
        document.setSample(0, frame, float(frame % 19) / 20, false);
        document.setSample(1, frame, -float(frame % 23) / 24, false);
    }
    session.rebuildWaveformCacheSynchronously();
    state.getActiveViewState().selectedChannels =
        cupuacu::SelectedChannels::LEFT;
    int64_t start = 127;
    int64_t end = 257;
    SECTION("Crossing base block boundaries") {}
    SECTION("Partial last block")
    {
        start = 1025;
        end = 1031;
    }
    SECTION("Whole selected channel")
    {
        start = 0;
        end = 1031;
    }
    session.selection.setValue1(start);
    session.selection.setValue2(end);
    REQUIRE(cupuacu::actions::effects::queueAmplifyFade(
        &state, cupuacu::effects::AmplifyFadeSettings{50, 50, 0, true}));
    cupuacu::test::drainPendingEffectWork(&state);
    session.stopWaveformCacheBuild();
    const auto dirty = session.getWaveformCache(0).snapshotBuildState();
    REQUIRE(dirty.dirtyFromBlock == start / 128);
    REQUIRE(dirty.dirtyToBlock == (end - 1) / 128);
    REQUIRE_FALSE(session.getWaveformCache(1).hasDirtyBlocks());

    // Undo before any newly built peaks are applied, then redo that dirty
    // cache.
    state.undo();
    REQUIRE_FALSE(session.getWaveformCache(0).hasDirtyBlocks());
    requireMatchingPeaks(session);
    state.redo();
    requireMatchingPeaks(session);
    // A fully built effect revision also survives another undo/redo cycle.
    state.undo();
    requireMatchingPeaks(session);
    state.redo();
    REQUIRE_FALSE(session.getWaveformCache(0).hasDirtyBlocks());
    requireMatchingPeaks(session);
}

TEST_CASE("Background effects freeze source peaks and preserve pending work",
          "[effects][waveform]")
{
    cupuacu::DocumentSession session;
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 1024);
    session.rebuildWaveformCacheSynchronously();
    bool expectFullBuild = false;
    SECTION("Clean source") {}
    SECTION("Pending cache build")
    {
        session.document.setSample(1, 900, 0.75f, false);
        session.getWaveformCache(1).invalidateSample(900);
        session.updateWaveformCache();
    }
    SECTION("Stale cache")
    {
        session.document.setSample(1, 900, 0.75f, false);
        expectFullBuild = true;
    }
    SECTION("Missing cache")
    {
        session.waveformCaches.resetToChannelCount(2);
        expectFullBuild = true;
    }
    cupuacu::actions::effects::BackgroundEffectRequest request;
    request.kind = cupuacu::actions::effects::BackgroundEffectKind::Reverse;
    request.startFrame = 127;
    request.frameCount = 130;
    request.targetChannels = {0};
    cupuacu::actions::effects::BackgroundEffectJob job(
        1, request, session.document, {}, &session.waveformCaches);
    // Changing the live revision must not affect the job's cache snapshot.
    session.document.setSample(1, 10, -0.8f, false);
    session.rebuildWaveformCacheSynchronously();
    job.start();
    REQUIRE(job.waitForCompletion(std::chrono::seconds(5)));
    REQUIRE(job.snapshot().success);
    auto result = job.takeResult();
    REQUIRE(result->preparedDocument.has_value());
    session.document = std::move(*result->preparedDocument);
    session.waveformCaches = std::move(result->preparedWaveformCaches);
    if (expectFullBuild)
    {
        REQUIRE(session.getWaveformCache(0).levelsCount() == 0);
    }
    else
    {
        const auto dirty = session.getWaveformCache(0).snapshotBuildState();
        REQUIRE(dirty.dirtyFromBlock == 0);
        REQUIRE(dirty.dirtyToBlock == 2);
    }
    requireMatchingPeaks(session);
}

TEST_CASE("Remove silence keeps waveform caches aligned with prepared audio",
          "[effects][waveform]")
{
    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 100, 2, 1031);
    for (int channel = 0; channel < 2; ++channel)
    {
        for (int64_t frame = 0; frame < 1031; ++frame)
        {
            session.document.setSample(
                channel, frame, frame >= 200 && frame < 400 ? 0.0f : 0.5f,
                false);
        }
    }
    session.rebuildWaveformCacheSynchronously();
    bool removesDuration = true;
    SECTION("Remove duration") {}
    SECTION("Compact selected channel")
    {
        removesDuration = false;
        state.getActiveViewState().selectedChannels =
            cupuacu::SelectedChannels::LEFT;
    }
    session.selection.setValue1(128);
    session.selection.setValue2(512);
    cupuacu::effects::RemoveSilenceSettings settings{};
    settings.modeIndex = 1;
    settings.thresholdUnitIndex = 1;
    settings.thresholdSampleValue = 0.01;
    settings.minimumSilenceLengthMs = 10.0;
    REQUIRE(cupuacu::actions::effects::queueRemoveSilence(&state, settings));
    cupuacu::test::drainPendingEffectWork(&state);
    REQUIRE(session.document.getFrameCount() == (removesDuration ? 831 : 1031));
    if (!removesDuration)
    {
        REQUIRE_FALSE(session.getWaveformCache(1).hasDirtyBlocks());
        const auto dirty = session.getWaveformCache(0).snapshotBuildState();
        REQUIRE(dirty.dirtyFromBlock == 1);
        REQUIRE(dirty.dirtyToBlock == 3);
    }
    requireMatchingPeaks(session);
    state.undo();
    requireMatchingPeaks(session);
    state.redo();
    requireMatchingPeaks(session);
}
