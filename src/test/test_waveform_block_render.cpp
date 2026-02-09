#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "TestStateBuilders.hpp"
#include "gui/Waveform.hpp"
#include "gui/WaveformCache.hpp"

#include <cmath>

TEST_CASE("Block render anchor and phase are stable within one pixel span",
          "[gui]")
{
    constexpr double spp = 100.0;

    REQUIRE(cupuacu::gui::Waveform::getBlockRenderSampleAnchor(0, spp) == 0.0);
    REQUIRE(cupuacu::gui::Waveform::getBlockRenderSampleAnchor(1, spp) == 0.0);
    REQUIRE(cupuacu::gui::Waveform::getBlockRenderSampleAnchor(99, spp) == 0.0);
    REQUIRE(cupuacu::gui::Waveform::getBlockRenderSampleAnchor(100, spp) ==
            100.0);

    REQUIRE(cupuacu::gui::Waveform::getBlockRenderPhasePixels(0, spp) == 0.0);
    REQUIRE(cupuacu::gui::Waveform::getBlockRenderPhasePixels(1, spp) ==
            Catch::Approx(0.01));
    REQUIRE(cupuacu::gui::Waveform::getBlockRenderPhasePixels(50, spp) ==
            Catch::Approx(0.5));
    REQUIRE(cupuacu::gui::Waveform::getBlockRenderPhasePixels(99, spp) ==
            Catch::Approx(0.99));
    REQUIRE(cupuacu::gui::Waveform::getBlockRenderPhasePixels(100, spp) == 0.0);
}

TEST_CASE("Block mode keeps cache peak window stable while scrolling at fixed zoom",
          "[gui]")
{
    cupuacu::State state{};
    [[maybe_unused]] auto ui = cupuacu::test::createSessionUi(&state, 800000);

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 700.0; // block/cache mode

    auto &cache = state.activeDocumentSession.document.getWaveformCache(0);
    const int level = cache.getLevelIndex(viewState.samplesPerPixel);
    const int64_t samplesPerPeak = cache.samplesPerPeakForLevel(level);
    REQUIRE(samplesPerPeak > 0);

    const int x = 25;
    const int64_t baseOffset = 12000;

    double a0 = 0.0;
    double b0 = 0.0;
    cupuacu::gui::Waveform::getBlockRenderSampleWindowForPixel(
        x, baseOffset, viewState.samplesPerPixel, a0, b0);
    const int64_t baseI0 = static_cast<int64_t>(std::floor(a0)) / samplesPerPeak;
    const int64_t baseI1 =
        (static_cast<int64_t>(std::floor(b0)) - 1) / samplesPerPeak;
    const double basePhase =
        cupuacu::gui::Waveform::getBlockRenderPhasePixels(
            baseOffset, viewState.samplesPerPixel);
    const double anchor =
        cupuacu::gui::Waveform::getBlockRenderSampleAnchor(
            baseOffset, viewState.samplesPerPixel);
    const int64_t deltasUntilNextAnchor = static_cast<int64_t>(
        std::ceil(anchor + viewState.samplesPerPixel -
                  static_cast<double>(baseOffset)));
    REQUIRE(deltasUntilNextAnchor > 1);

    for (int64_t delta = 1; delta < deltasUntilNextAnchor; ++delta)
    {
        const int64_t offset = baseOffset + delta;

        double a = 0.0;
        double b = 0.0;
        cupuacu::gui::Waveform::getBlockRenderSampleWindowForPixel(
            x, offset, viewState.samplesPerPixel, a, b);

        const int64_t i0 =
            static_cast<int64_t>(std::floor(a)) / samplesPerPeak;
        const int64_t i1 =
            (static_cast<int64_t>(std::floor(b)) - 1) / samplesPerPeak;
        REQUIRE(i0 == baseI0);
        REQUIRE(i1 == baseI1);

        const double phase =
            cupuacu::gui::Waveform::getBlockRenderPhasePixels(
                offset, viewState.samplesPerPixel);
        const double expectedPhase =
            std::fmod(basePhase +
                          static_cast<double>(delta) / viewState.samplesPerPixel,
                      1.0);
        REQUIRE(phase == Catch::Approx(expectedPhase).epsilon(1e-9));
    }
}

TEST_CASE("Block render phase stays monotonic for large offsets",
          "[gui]")
{
    constexpr double spp = 683.3333333333;
    constexpr int64_t baseOffset = 12'345'678;

    const double basePhase =
        cupuacu::gui::Waveform::getBlockRenderPhasePixels(baseOffset, spp);
    REQUIRE(basePhase >= 0.0);
    REQUIRE(basePhase < 1.0);

    double prevPhase = basePhase;
    for (int64_t delta = 1; delta < 200; ++delta)
    {
        const double phase = cupuacu::gui::Waveform::getBlockRenderPhasePixels(
            baseOffset + delta, spp);
        REQUIRE(phase >= 0.0);
        REQUIRE(phase < 1.0);

        const bool wrapped = phase < prevPhase;
        if (!wrapped)
        {
            REQUIRE(phase > prevPhase);
        }

        prevPhase = phase;
    }
}
