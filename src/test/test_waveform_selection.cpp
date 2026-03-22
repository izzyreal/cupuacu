#include <catch2/catch_test_macros.hpp>

#include "gui/Waveform.hpp"
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

TEST_CASE("Block-mode selection fill rect excludes partially covered boundary pixels",
          "[selection]")
{
    SDL_FRect rect{};

    // With 10 samples per pixel, [15,25) intersects pixels 1 and 2 but fully
    // covers neither, so the filled selection area should not include either.
    const bool ok = cupuacu::gui::Waveform::computeBlockModeSelectionFillRect(
        15, 25, 0, 10.0, 100, 20, rect);

    REQUIRE_FALSE(ok);
}

TEST_CASE("Block-mode selection fill rect keeps fully covered interior pixels",
          "[selection]")
{
    SDL_FRect rect{};

    // With 10 samples per pixel, [10,30) fully covers pixels 1 and 2.
    const bool ok = cupuacu::gui::Waveform::computeBlockModeSelectionFillRect(
        10, 30, 0, 10.0, 100, 20, rect);

    REQUIRE(ok);
    REQUIRE(rect.x == 1.0f);
    REQUIRE(rect.w == 2.0f);
    REQUIRE(rect.h == 20.0f);
}

TEST_CASE("Block-mode selection rect covers any pixel whose block window intersects the selection",
          "[selection]")
{
    const int64_t firstSample = 42152;
    const int64_t lastSampleExclusive = 84376;
    const double samplesPerPixel = 84448.0 / 2528.0;
    SDL_FRect rect{};

    const bool ok = cupuacu::gui::Waveform::computeBlockModeSelectionRect(
        firstSample, lastSampleExclusive, 0, samplesPerPixel, 2528, 20, rect);

    REQUIRE(ok);
    REQUIRE(rect.x == 1261.0f);

    double windowStart = 0.0;
    double windowEnd = 0.0;
    cupuacu::gui::Waveform::getBlockRenderSampleWindowForPixel(
        1261, 0, samplesPerPixel, windowStart, windowEnd);

    REQUIRE(windowStart < static_cast<double>(firstSample));
    REQUIRE(windowEnd > static_cast<double>(firstSample));
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

    REQUIRE(anchor == 15.0);
    REQUIRE(phase == 0.0);
    REQUIRE(pixel0Start == 15.0);
    REQUIRE(pixel0End == 25.0);
    REQUIRE(pixel1Start == 25.0);
    REQUIRE(pixel1End == 35.0);
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

    REQUIRE(cupuacu::gui::shouldRenderWaveformSamplePoints(-1, 0.01, 1));
    REQUIRE_FALSE(cupuacu::gui::shouldRenderWaveformSamplePoints(42, 0.01, 1));
    REQUIRE_FALSE(cupuacu::gui::shouldRenderWaveformSamplePoints(-1, 0.1, 1));

    const auto highlightRect =
        cupuacu::gui::planWaveformHighlightRect(true, 15, 32, 10, 2.0, 20);
    REQUIRE(highlightRect.visible);
    REQUIRE(highlightRect.rect.x == 3.0f);
    REQUIRE(highlightRect.rect.w == 0.5f);

    const auto hiddenHighlightRect =
        cupuacu::gui::planWaveformHighlightRect(false, 15, 32, 10, 2.0, 20);
    REQUIRE_FALSE(hiddenHighlightRect.visible);

    const auto invalidHighlightRect =
        cupuacu::gui::planWaveformHighlightRect(true, 40, 32, 10, 2.0, 20);
    REQUIRE_FALSE(invalidHighlightRect.visible);

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

    const auto hiddenSelectionRect =
        cupuacu::gui::planWaveformLinearSelectionRect(false, 12, 16, 10, 2.0,
                                                      20);
    REQUIRE_FALSE(hiddenSelectionRect.visible);
}
