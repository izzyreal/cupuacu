#include <catch2/catch_test_macros.hpp>

#include "TestStateBuilders.hpp"
#include "gui/Waveform.hpp"
#include "gui/WaveformRefresh.hpp"
#include "gui/SamplePoint.hpp"
#include "gui/WaveformVisualState.hpp"

TEST_CASE("Block-mode selection rect covers boundary pixels for sample ranges",
          "[selection]")
{
    SDL_FRect rect{};

    // With 10 samples per pixel, [15,25) spans pixels 1 and 2.
    const bool ok = cupuacu::gui::Waveform::computeBlockModeSelectionRect(
        15, 25, 0, 10.0, 100, 20, rect);

    REQUIRE(ok);
    REQUIRE(rect.x == 1.0f);
    REQUIRE(rect.w == 2.0f);
    REQUIRE(rect.h == 20.0f);
}

TEST_CASE("Block-mode selection rect expands to cache-bin boundaries",
          "[selection]")
{
    SDL_FRect rect{};

    // With 10 samples/pixel and 16-sample cache bins, [15,25) becomes [0,32).
    // Pixel span is then [0,4), which matches what cache-mode waveform draws.
    const bool ok = cupuacu::gui::Waveform::computeBlockModeSelectionRect(
        15, 25, 0, 10.0, 100, 20, rect, 16);

    REQUIRE(ok);
    REQUIRE(rect.x == 0.0f);
    REQUIRE(rect.w == 4.0f);
}

TEST_CASE("Block-mode selection rect can include connector pixel padding",
          "[selection]")
{
    SDL_FRect rect{};

    // Without padding [15,25) at 10 samples/pixel is [1,3), width 2.
    // With connector padding we include one pixel on both sides => [0,4).
    const bool ok = cupuacu::gui::Waveform::computeBlockModeSelectionRect(
        15, 25, 0, 10.0, 100, 20, rect, 1, true);

    REQUIRE(ok);
    REQUIRE(rect.x == 0.0f);
    REQUIRE(rect.w == 4.0f);
}

TEST_CASE("Block-mode selection edge pixels match rect edges", "[selection]")
{
    SDL_FRect rect{};
    int32_t startEdge = -1;
    int32_t endEdgeExclusive = -1;

    const bool rectOk = cupuacu::gui::Waveform::computeBlockModeSelectionRect(
        15, 25, 0, 10.0, 100, 20, rect, 16, true);
    const bool edgeOk =
        cupuacu::gui::Waveform::computeBlockModeSelectionEdgePixels(
            15, 25, 0, 10.0, 100, startEdge, endEdgeExclusive, 16, true);

    REQUIRE(rectOk);
    REQUIRE(edgeOk);
    REQUIRE(startEdge == static_cast<int32_t>(rect.x));
    REQUIRE(endEdgeExclusive == static_cast<int32_t>(rect.x + rect.w));
}

TEST_CASE("Block render anchor, phase, and sample windows stay aligned",
          "[selection]")
{
    const double anchor =
        cupuacu::gui::Waveform::getBlockRenderSampleAnchor(15, 10.0);
    const double phase =
        cupuacu::gui::Waveform::getBlockRenderPhasePixels(15, 10.0);

    double pixel0Start = 0.0;
    double pixel0End = 0.0;
    double pixel1Start = 0.0;
    double pixel1End = 0.0;
    cupuacu::gui::Waveform::getBlockRenderSampleWindowForPixel(
        0, 15, 10.0, pixel0Start, pixel0End);
    cupuacu::gui::Waveform::getBlockRenderSampleWindowForPixel(
        1, 15, 10.0, pixel1Start, pixel1End);

    REQUIRE(anchor == 10.0);
    REQUIRE(phase == 0.5);
    REQUIRE(pixel0Start == 10.0);
    REQUIRE(pixel0End == 20.0);
    REQUIRE(pixel1Start == 20.0);
    REQUIRE(pixel1End == 30.0);
}

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

TEST_CASE("Waveform block-mode selection rect rejects invalid or offscreen inputs",
          "[selection]")
{
    SDL_FRect rect{};

    REQUIRE_FALSE(cupuacu::gui::Waveform::computeBlockModeSelectionRect(
        10, 10, 0, 10.0, 100, 20, rect));
    REQUIRE_FALSE(cupuacu::gui::Waveform::computeBlockModeSelectionRect(
        10, 20, 0, 0.0, 100, 20, rect));
    REQUIRE_FALSE(cupuacu::gui::Waveform::computeBlockModeSelectionRect(
        10, 20, 1000, 10.0, 10, 20, rect));
}

TEST_CASE("Waveform visual planning helpers compute markers and rects",
          "[selection]")
{
    const auto playbackMarker =
        cupuacu::gui::planWaveformPlaybackMarker(15, 10, 2.0, 10);
    REQUIRE(playbackMarker.visible);
    REQUIRE(playbackMarker.x == 3);

    const auto hiddenPlaybackMarker =
        cupuacu::gui::planWaveformPlaybackMarker(-1, 10, 2.0, 10);
    REQUIRE_FALSE(hiddenPlaybackMarker.visible);

    const auto cursorMarker =
        cupuacu::gui::planWaveformCursorMarker(false, 15, 10, 2.0, 10);
    REQUIRE(cursorMarker.visible);
    REQUIRE(cursorMarker.x == 3);

    const auto hiddenCursorMarker =
        cupuacu::gui::planWaveformCursorMarker(true, 15, 10, 2.0, 10);
    REQUIRE_FALSE(hiddenCursorMarker.visible);

    const auto highlightRect =
        cupuacu::gui::planWaveformHighlightRect(true, 15, 32, 10, 2.0, 20);
    REQUIRE(highlightRect.visible);
    REQUIRE(highlightRect.rect.x == 3.0f);
    REQUIRE(highlightRect.rect.w == 0.5f);

    const auto hiddenHighlightRect =
        cupuacu::gui::planWaveformHighlightRect(false, 15, 32, 10, 2.0, 20);
    REQUIRE_FALSE(hiddenHighlightRect.visible);

    const auto selectionRect = cupuacu::gui::planWaveformLinearSelectionRect(
        true, 12, 16, 10, 2.0, 20);
    REQUIRE(selectionRect.visible);
    REQUIRE(selectionRect.rect.x == 1.0f);
    REQUIRE(selectionRect.rect.w == 2.0f);

    const auto minimumWidthSelectionRect =
        cupuacu::gui::planWaveformLinearSelectionRect(true, 10, 11, 10, 2.0,
                                                      20);
    REQUIRE(minimumWidthSelectionRect.visible);
    REQUIRE(minimumWidthSelectionRect.rect.w == 1.0f);
}
