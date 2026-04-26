#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestPaths.hpp"
#include "actions/effects/BackgroundEffect.hpp"
#include "effects/RemoveSilenceEffect.hpp"
#include "gui/DevicePropertiesWindow.hpp"

using Catch::Approx;

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

        FAIL("Timed out waiting for background remove silence work");
    }
}

TEST_CASE("Remove silence planner trims only edges in edge mode", "[effects]")
{
    cupuacu::Document doc{};
    doc.initialize(cupuacu::SampleFormat::FLOAT32, 100, 1, 8);
    doc.setSample(0, 2, 0.5f, false);
    doc.setSample(0, 3, 0.5f, false);
    doc.setSample(0, 4, 0.5f, false);
    doc.setSample(0, 5, 0.5f, false);

    cupuacu::effects::RemoveSilenceSettings settings{};
    settings.minimumSilenceLengthMs = 10.0;

    const auto runs = cupuacu::effects::planSilenceRemoval(
        doc, {0}, 0, 8, 0.01,
        cupuacu::effects::RemoveSilenceMode::FromBeginningAndEnd, settings);

    REQUIRE(runs.size() == 2);
    REQUIRE(runs[0].startFrame == 0);
    REQUIRE(runs[0].frameCount == 2);
    REQUIRE(runs[1].startFrame == 6);
    REQUIRE(runs[1].frameCount == 2);
}

TEST_CASE("Remove silence planner captures interior silent runs in all-silences mode",
          "[effects]")
{
    cupuacu::Document doc{};
    doc.initialize(cupuacu::SampleFormat::FLOAT32, 100, 1, 8);
    doc.setSample(0, 0, 0.5f, false);
    doc.setSample(0, 3, 0.5f, false);
    doc.setSample(0, 7, 0.5f, false);

    cupuacu::effects::RemoveSilenceSettings settings{};
    settings.minimumSilenceLengthMs = 10.0;

    const auto runs = cupuacu::effects::planSilenceRemoval(
        doc, {0}, 0, 8, 0.01,
        cupuacu::effects::RemoveSilenceMode::AllSilencesInSection, settings);

    REQUIRE(runs.size() == 2);
    REQUIRE(runs[0].startFrame == 1);
    REQUIRE(runs[0].frameCount == 2);
    REQUIRE(runs[1].startFrame == 4);
    REQUIRE(runs[1].frameCount == 3);
}

TEST_CASE("Remove silence apply removes planned silent ranges", "[effects]")
{
    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();
    auto &doc = session.document;
    doc.initialize(cupuacu::SampleFormat::FLOAT32, 100, 1, 8);
    doc.setSample(0, 0, 0.5f, false);
    doc.setSample(0, 3, 0.4f, false);
    doc.setSample(0, 7, 0.3f, false);

    cupuacu::effects::RemoveSilenceSettings settings{};
    settings.modeIndex = 1;
    settings.thresholdUnitIndex = 1;
    settings.thresholdSampleValue = 0.01;
    settings.minimumSilenceLengthMs = 10.0;

    cupuacu::effects::performRemoveSilence(&state, settings);
    drainPendingEffectWork(&state);

    REQUIRE(doc.getFrameCount() == 3);
    REQUIRE(doc.getSample(0, 0) == Approx(0.5f));
    REQUIRE(doc.getSample(0, 1) == Approx(0.4f));
    REQUIRE(doc.getSample(0, 2) == Approx(0.3f));
}

TEST_CASE("Remove silence planner ignores sub-minimum micro gaps", "[effects]")
{
    cupuacu::Document doc{};
    doc.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, 12);
    for (int64_t frame = 0; frame < doc.getFrameCount(); ++frame)
    {
        doc.setSample(0, frame, 0.4f, false);
    }
    doc.setSample(0, 5, 0.0f, false);
    doc.setSample(0, 6, 0.0f, false);

    cupuacu::effects::RemoveSilenceSettings settings{};
    settings.minimumSilenceLengthMs = 10.0;

    const auto runs = cupuacu::effects::planSilenceRemoval(
        doc, {0}, 0, doc.getFrameCount(), 0.01,
        cupuacu::effects::RemoveSilenceMode::AllSilencesInSection, settings);

    REQUIRE(runs.empty());
}

TEST_CASE("Remove silence apply preserves stereo audio when only micro gaps match the threshold",
          "[effects]")
{
    cupuacu::test::StateWithTestPaths state{};
    auto &doc = state.getActiveDocumentSession().document;
    doc.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 16);

    for (int64_t frame = 0; frame < doc.getFrameCount(); ++frame)
    {
        const float left = (frame % 2 == 0) ? 0.25f : -0.25f;
        const float right = (frame % 2 == 0) ? -0.5f : 0.5f;
        doc.setSample(0, frame, left, false);
        doc.setSample(1, frame, right, false);
    }

    doc.setSample(0, 4, 0.0f, false);
    doc.setSample(0, 11, 0.0f, false);

    cupuacu::effects::RemoveSilenceSettings settings{};
    settings.modeIndex = 1;
    settings.thresholdUnitIndex = 1;
    settings.thresholdSampleValue = 0.01;
    settings.minimumSilenceLengthMs = 10.0;

    cupuacu::effects::performRemoveSilence(&state, settings);
    drainPendingEffectWork(&state);

    REQUIRE(doc.getFrameCount() == 16);
    REQUIRE(doc.getSample(0, 0) == Approx(0.25f));
    REQUIRE(doc.getSample(0, 4) == Approx(0.0f));
    REQUIRE(doc.getSample(0, 11) == Approx(0.0f));
    REQUIRE(doc.getSample(1, 0) == Approx(-0.5f));
    REQUIRE(doc.getSample(1, 4) == Approx(-0.5f));
    REQUIRE(doc.getSample(1, 11) == Approx(0.5f));
}

TEST_CASE("Remove silence planner follows configured minimum silence length",
          "[effects]")
{
    cupuacu::Document doc{};
    doc.initialize(cupuacu::SampleFormat::FLOAT32, 1000, 1, 40);
    for (int64_t frame = 0; frame < doc.getFrameCount(); ++frame)
    {
        doc.setSample(0, frame, 0.4f, false);
    }
    for (int64_t frame = 10; frame < 18; ++frame)
    {
        doc.setSample(0, frame, 0.0f, false);
    }

    cupuacu::effects::RemoveSilenceSettings shortMinimum{};
    shortMinimum.minimumSilenceLengthMs = 5.0;
    auto runs = cupuacu::effects::planSilenceRemoval(
        doc, {0}, 0, doc.getFrameCount(), 0.01,
        cupuacu::effects::RemoveSilenceMode::AllSilencesInSection,
        shortMinimum);
    REQUIRE(runs.size() == 1);
    REQUIRE(runs[0].startFrame == 10);
    REQUIRE(runs[0].frameCount == 8);

    cupuacu::effects::RemoveSilenceSettings longMinimum{};
    longMinimum.minimumSilenceLengthMs = 12.0;
    runs = cupuacu::effects::planSilenceRemoval(
        doc, {0}, 0, doc.getFrameCount(), 0.01,
        cupuacu::effects::RemoveSilenceMode::AllSilencesInSection,
        longMinimum);
    REQUIRE(runs.empty());
}

TEST_CASE("Remove silence compacts only the selected stereo channel", "[effects]")
{
    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();
    auto &doc = session.document;
    doc.initialize(cupuacu::SampleFormat::FLOAT32, 100, 2, 8);

    const float leftSamples[8] = {1.0f, 0.0f, 0.0f, 2.0f,
                                  3.0f, 0.0f, 0.0f, 4.0f};
    const float rightSamples[8] = {10.0f, 11.0f, 12.0f, 13.0f,
                                   14.0f, 15.0f, 16.0f, 17.0f};
    for (int64_t frame = 0; frame < 8; ++frame)
    {
        doc.setSample(0, frame, leftSamples[frame], false);
        doc.setSample(1, frame, rightSamples[frame], false);
    }

    session.selection.setValue1(0.0);
    session.selection.setValue2(8.0);
    state.getActiveViewState().selectedChannels = cupuacu::SelectedChannels::LEFT;

    cupuacu::effects::RemoveSilenceSettings settings{};
    settings.modeIndex = 1;
    settings.thresholdUnitIndex = 1;
    settings.thresholdSampleValue = 0.01;
    settings.minimumSilenceLengthMs = 10.0;

    cupuacu::effects::performRemoveSilence(&state, settings);
    drainPendingEffectWork(&state);

    REQUIRE(doc.getFrameCount() == 8);
    REQUIRE(doc.getSample(0, 0) == Approx(1.0f));
    REQUIRE(doc.getSample(0, 1) == Approx(2.0f));
    REQUIRE(doc.getSample(0, 2) == Approx(3.0f));
    REQUIRE(doc.getSample(0, 3) == Approx(4.0f));
    REQUIRE(doc.getSample(0, 4) == Approx(0.0f));
    REQUIRE(doc.getSample(0, 7) == Approx(0.0f));
    REQUIRE(doc.getSample(1, 0) == Approx(10.0f));
    REQUIRE(doc.getSample(1, 1) == Approx(11.0f));
    REQUIRE(doc.getSample(1, 7) == Approx(17.0f));
}
