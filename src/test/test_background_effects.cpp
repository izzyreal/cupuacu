#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestPaths.hpp"
#include "actions/effects/BackgroundEffect.hpp"
#include "effects/AmplifyFadeEffect.hpp"
#include "effects/AmplifyEnvelopeEffect.hpp"
#include "effects/DynamicsEffect.hpp"
#include "effects/RemoveSilenceEffect.hpp"
#include "effects/ReverseEffect.hpp"

#include <chrono>
#include <thread>

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
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        FAIL("Timed out waiting for background effect work");
    }
}

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

    cupuacu::effects::performReverse(&state);

    REQUIRE(state.backgroundEffectJob != nullptr);
    REQUIRE(state.longTask.active);
    REQUIRE(state.longTask.title == "Applying effect");
    REQUIRE(state.longTask.detail == "Reverse");
    REQUIRE_FALSE(state.canUndo());

    drainPendingEffectWork(&state);

    REQUIRE(state.backgroundEffectJob == nullptr);
    REQUIRE_FALSE(state.longTask.active);
    REQUIRE(state.canUndo());
    REQUIRE(state.getUndoDescription() == "Reverse");
    REQUIRE(document.getSample(0, 0) == Catch::Approx(0.4f));
    REQUIRE(document.getSample(0, 1) == Catch::Approx(0.3f));
    REQUIRE(document.getSample(0, 2) == Catch::Approx(0.2f));
    REQUIRE(document.getSample(0, 3) == Catch::Approx(0.1f));

    state.undo();
    REQUIRE(document.getSample(0, 0) == Catch::Approx(0.1f));
    REQUIRE(document.getSample(0, 1) == Catch::Approx(0.2f));
    REQUIRE(document.getSample(0, 2) == Catch::Approx(0.3f));
    REQUIRE(document.getSample(0, 3) == Catch::Approx(0.4f));

    state.redo();
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

    drainPendingEffectWork(&state);

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

    drainPendingEffectWork(&state);

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

    drainPendingEffectWork(&state);

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
