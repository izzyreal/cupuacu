#include <catch2/catch_test_macros.hpp>

#include "TestStateBuilders.hpp"
#include "gui/SamplePoint.hpp"
#include "gui/Waveform.hpp"
#include "gui/WaveformRefresh.hpp"
#include "gui/WaveformVisualState.hpp"

TEST_CASE("Waveform sample point threshold and coordinate helpers are stable",
          "[selection]")
{
    const double threshold =
        static_cast<double>(1.0f / 40.0f);
    REQUIRE(cupuacu::gui::Waveform::shouldShowSamplePoints(0.01, 1));
    REQUIRE(cupuacu::gui::Waveform::shouldShowSamplePoints(0.024, 1));
    REQUIRE(cupuacu::gui::Waveform::shouldShowSamplePoints(threshold * 0.999, 1));
    REQUIRE_FALSE(
        cupuacu::gui::Waveform::shouldShowSamplePoints(threshold * 1.001, 1));
    REQUIRE_FALSE(cupuacu::gui::Waveform::shouldShowSamplePoints(0.05, 1));
    REQUIRE(cupuacu::gui::Waveform::shouldShowSamplePoints(0.024, 2));
    REQUIRE(cupuacu::gui::Waveform::shouldShowSamplePoints(0.049, 2));
    REQUIRE(cupuacu::gui::Waveform::shouldShowSamplePoints(0.09, 4));
    REQUIRE_FALSE(
        cupuacu::gui::Waveform::shouldShowSamplePoints(0.051, 2));
    REQUIRE_FALSE(
        cupuacu::gui::Waveform::shouldShowSamplePoints(0.101, 4));

    REQUIRE(cupuacu::gui::Waveform::getDoubleXPosForSampleIndex(15, 10, 2.0) ==
            2.5);
    REQUIRE(cupuacu::gui::Waveform::getXPosForSampleIndex(15, 10, 2.0) == 3);
    REQUIRE(cupuacu::gui::Waveform::getDoubleSampleIndexForXPos(2.5f, 10, 2.0) ==
            15.0);
    REQUIRE(cupuacu::gui::Waveform::getSampleIndexForXPos(2.5f, 10, 2.0) == 15);

    REQUIRE(cupuacu::gui::shouldRenderWaveformSamplePoints(-1, 0.01, 1));
    REQUIRE_FALSE(cupuacu::gui::shouldRenderWaveformSamplePoints(5, 0.01, 1));
}

TEST_CASE("Waveform sample points hide during playback and return afterwards",
          "[selection]")
{
    cupuacu::State state{};
    auto ui = cupuacu::test::createSessionUi(&state, 128, false, 2);
    REQUIRE(state.waveforms.size() == 2);

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 0.01;
    cupuacu::gui::refreshWaveforms(&state, true, false);

    REQUIRE_FALSE(state.waveforms[0]->getChildren().empty());

    state.waveforms[0]->setPlaybackPosition(5);
    REQUIRE(state.waveforms[0]->getChildren().empty());

    state.waveforms[0]->setPlaybackPosition(-1);
    REQUIRE_FALSE(state.waveforms[0]->getChildren().empty());
}

TEST_CASE("Waveform clearHighlightIfNotChannel clears only other channels",
          "[selection]")
{
    cupuacu::State state{};
    auto ui = cupuacu::test::createSessionUi(&state, 64, false, 2);
    REQUIRE(state.waveforms.size() == 2);

    state.waveforms[0]->setSamplePosUnderCursor(5);
    state.waveforms[1]->setSamplePosUnderCursor(9);

    cupuacu::gui::Waveform::clearHighlightIfNotChannel(&state, 1);

    REQUIRE_FALSE(state.waveforms[0]->getSamplePosUnderCursor().has_value());
    REQUIRE(state.waveforms[1]->getSamplePosUnderCursor() == 9);
}

TEST_CASE("Waveform highlight state can be set, cleared, and reset directly",
          "[selection]")
{
    cupuacu::State state{};
    auto ui = cupuacu::test::createSessionUi(&state, 64, false, 2);
    auto *waveform = state.waveforms[0];

    REQUIRE_FALSE(waveform->getSamplePosUnderCursor().has_value());

    waveform->setSamplePosUnderCursor(7);
    REQUIRE(waveform->getSamplePosUnderCursor() == 7);

    waveform->clearHighlight();
    REQUIRE_FALSE(waveform->getSamplePosUnderCursor().has_value());

    waveform->setSamplePosUnderCursor(11);
    REQUIRE(waveform->getSamplePosUnderCursor() == 11);
    waveform->resetSamplePosUnderCursor();
    REQUIRE_FALSE(waveform->getSamplePosUnderCursor().has_value());
}

TEST_CASE("Waveform mouseLeave preserves highlight when entering a sample point",
          "[selection]")
{
    cupuacu::State state{};
    auto ui = cupuacu::test::createSessionUi(&state, 64, false, 2);
    REQUIRE_FALSE(state.waveforms.empty());

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 0.01;
    cupuacu::gui::refreshWaveforms(&state, true, false);

    auto *waveform = state.waveforms[0];
    REQUIRE_FALSE(waveform->getChildren().empty());
    auto *samplePoint =
        dynamic_cast<cupuacu::gui::SamplePoint *>(waveform->getChildren().front().get());
    REQUIRE(samplePoint != nullptr);

    waveform->setSamplePosUnderCursor(3);
    waveform->getWindow()->setComponentUnderMouse(samplePoint);
    waveform->mouseLeave();

    REQUIRE(waveform->getSamplePosUnderCursor() == 3);

    waveform->getWindow()->setComponentUnderMouse(nullptr);
    waveform->mouseLeave();
    REQUIRE_FALSE(waveform->getSamplePosUnderCursor().has_value());
}
