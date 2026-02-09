#include <catch2/catch_test_macros.hpp>

#include "gui/Waveform.hpp"

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
