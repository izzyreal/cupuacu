#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "TestStateBuilders.hpp"
#include "actions/audio/EffectCommands.hpp"
#include "gui/DocumentSessionWindow.hpp"
#include "gui/Gui.hpp"
#include "gui/SamplePointInteractionPlanning.hpp"
#include "gui/WaveformBlockRenderPlanning.hpp"
#include "gui/WaveformSamplePointPlanning.hpp"
#include "gui/WaveformSmoothRenderPlanning.hpp"
#include "gui/Waveform.hpp"
#include "gui/WaveformCache.hpp"

#include <cmath>
#include <memory>

namespace
{
    struct RgbaPixel
    {
        Uint8 r = 0;
        Uint8 g = 0;
        Uint8 b = 0;
        Uint8 a = 0;
    };

    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>
    readWindowCanvas(cupuacu::gui::Window *window)
    {
        auto *renderer = window->getRenderer();
        SDL_SetRenderTarget(renderer, window->getCanvas());
        SDL_Surface *surface = SDL_RenderReadPixels(renderer, nullptr);
        SDL_SetRenderTarget(renderer, nullptr);
        return {surface, SDL_DestroySurface};
    }

    RgbaPixel readSurfacePixel(const SDL_Surface *surface, const int x,
                               const int y)
    {
        const auto *pixels =
            static_cast<const Uint8 *>(surface->pixels) +
            y * surface->pitch + x * 4;
        return {pixels[0], pixels[1], pixels[2], pixels[3]};
    }

    int computeWaveformColumnHeight(const SDL_Surface *surface,
                                    const SDL_Rect waveformBounds,
                                    const int x)
    {
        int top = waveformBounds.y + waveformBounds.h;
        int bottom = waveformBounds.y;
        bool found = false;

        for (int y = waveformBounds.y; y < waveformBounds.y + waveformBounds.h;
             ++y)
        {
            const auto pixel = readSurfacePixel(surface, x, y);
            const bool looksLikeWaveformGreen =
                pixel.g > 100 && pixel.g > pixel.r + 40 &&
                pixel.g > pixel.b + 40;
            if (!looksLikeWaveformGreen)
            {
                continue;
            }

            found = true;
            top = std::min(top, y);
            bottom = std::max(bottom, y);
        }

        return found ? (bottom - top + 1) : 0;
    }
}

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

TEST_CASE("Block waveform planner deduplicates columns and tracks connectors",
          "[gui]")
{
    const auto plan = cupuacu::gui::planBlockWaveformColumns(
        4, 0.4, 10, 10.0f,
        [](const int x, cupuacu::gui::Peak &peak) -> bool
        {
            switch (x)
            {
                case 0:
                    peak = {-0.5f, 0.5f};
                    return true;
                case 1:
                    peak = {-0.25f, 0.75f};
                    return true;
                case 2:
                    peak = {-0.25f, 0.75f};
                    return true;
                case 3:
                    peak = {-1.0f, 1.0f};
                    return true;
                default:
                    return false;
            }
        });

    REQUIRE(plan.size() == 4);

    REQUIRE(plan[0].drawXi == 0);
    REQUIRE_FALSE(plan[0].connectFromPrevious);
    REQUIRE(plan[0].y1 == 5);
    REQUIRE(plan[0].y2 == 15);

    REQUIRE(plan[1].drawXi == 1);
    REQUIRE(plan[1].connectFromPrevious);
    REQUIRE(plan[1].previousX == 0);
    REQUIRE(plan[1].previousY == 10);

    REQUIRE(plan[2].drawXi == 2);
    REQUIRE(plan[2].connectFromPrevious);
    REQUIRE(plan[2].previousX == 1);

    REQUIRE(plan[3].drawXi == 3);
    REQUIRE(plan[3].connectFromPrevious);
    REQUIRE(plan[3].previousX == 2);
}

TEST_CASE("Block waveform planner skips peaks that map offscreen",
          "[gui]")
{
    const auto plan = cupuacu::gui::planBlockWaveformColumns(
        2, 5.0, 10, 4.0f,
        [](const int x, cupuacu::gui::Peak &peak) -> bool
        {
            peak = {-0.5f, 0.5f};
            return x < 3;
        });

    REQUIRE(plan.empty());
}

TEST_CASE("Waveform sample point planner maps visible samples to point bounds",
          "[gui]")
{
    const auto plan = cupuacu::gui::planWaveformSamplePoints(
        4, 20, 0.5, 2, 2, 1.0, 10,
        [](const int64_t sampleIndex)
        {
            return sampleIndex == 2 ? 1.0f : (sampleIndex == 3 ? 0.0f : -1.0f);
        });

    REQUIRE(plan.size() == 2);

    REQUIRE(plan[0].sampleIndex == 2);
    REQUIRE(plan[0].size == 16);
    REQUIRE(plan[0].x == 0);
    REQUIRE(plan[0].y == 0);

    REQUIRE(plan[1].sampleIndex == 3);
    REQUIRE(plan[1].x == 0);
    REQUIRE(plan[1].y == 2);
}

TEST_CASE("Waveform sample point planner clamps to available input samples",
          "[gui]")
{
    const auto emptyPlan = cupuacu::gui::planWaveformSamplePoints(
        10, 20, 1.0, 12, 1, 1.0, 12,
        [](const int64_t)
        {
            return 0.0f;
        });
    REQUIRE(emptyPlan.empty());

    const auto truncatedPlan = cupuacu::gui::planWaveformSamplePoints(
        10, 20, 2.0, 8, 1, 1.0, 10,
        [](const int64_t sampleIndex)
        {
            return sampleIndex == 8 ? -0.5f : 0.5f;
        });

    REQUIRE(truncatedPlan.size() == 2);
    REQUIRE(truncatedPlan[0].sampleIndex == 8);
    REQUIRE(truncatedPlan[1].sampleIndex == 9);
}

TEST_CASE("Block waveform render does not keep full-height columns just inside a fade end",
          "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    state.pixelScale = 4;

    auto &session = state.activeDocumentSession;
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1,
                                800000);
    for (int64_t i = 0; i < session.document.getFrameCount(); ++i)
    {
        session.document.setSample(0, i, (i % 2 == 0) ? 1.0f : -1.0f);
    }

    state.mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            &state, &session, "test", 800, 400, SDL_WINDOW_HIDDEN);
    cupuacu::gui::buildComponents(
        &state, state.mainDocumentSessionWindow->getWindow());
    REQUIRE_FALSE(state.waveforms.empty());

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    const SDL_Rect waveformBounds = state.waveforms[0]->getAbsoluteBounds();
    const int64_t selectionEnd = 640000;
    const int targetEdgeX = waveformBounds.w * 4 / 5;

    viewState.sampleOffset = 0;
    viewState.samplesPerPixel =
        static_cast<double>(selectionEnd) / static_cast<double>(targetEdgeX);

    auto *window = state.mainDocumentSessionWindow->getWindow();
    window->renderFrame();

    session.selection.setValue1(0.0);
    session.selection.setValue2(static_cast<double>(selectionEnd));
    cupuacu::actions::audio::performAmplifyFade(&state, 100.0, 0.0, 0);
    REQUIRE(std::fabs(session.document.getSample(0, selectionEnd - 1)) < 0.01f);
    REQUIRE(std::fabs(session.document.getSample(0, selectionEnd)) > 0.9f);

    window->renderFrameIfDirty();

    auto surface = readWindowCanvas(window);
    REQUIRE(surface != nullptr);

    const int edgeX =
        waveformBounds.x + cupuacu::gui::Waveform::getXPosForSampleIndex(
                              selectionEnd, viewState.sampleOffset,
                              viewState.samplesPerPixel);
    const int insideHeight =
        computeWaveformColumnHeight(surface.get(), waveformBounds, edgeX - 2);
    const int outsideHeight =
        computeWaveformColumnHeight(surface.get(), waveformBounds, edgeX + 2);

    INFO("insideHeight=" << insideHeight << " outsideHeight=" << outsideHeight
                         << " edgeX=" << edgeX << " waveformWidth="
                         << waveformBounds.w << " spp="
                         << viewState.samplesPerPixel);
    REQUIRE(outsideHeight > 0);
    REQUIRE(insideHeight < outsideHeight / 2);
}

TEST_CASE("Block waveform render clips a phase-shifted fade boundary to the selected range",
          "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    state.pixelScale = 1;

    auto &session = state.activeDocumentSession;
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1,
                                150000);
    for (int64_t i = 0; i < session.document.getFrameCount(); ++i)
    {
        session.document.setSample(0, i, (i % 2 == 0) ? 1.0f : -1.0f);
    }

    state.mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            &state, &session, "test", 800, 400, SDL_WINDOW_HIDDEN);
    cupuacu::gui::buildComponents(
        &state, state.mainDocumentSessionWindow->getWindow());
    REQUIRE_FALSE(state.waveforms.empty());

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    const SDL_Rect waveformBounds = state.waveforms[0]->getAbsoluteBounds();
    const int64_t selectionStart = 50000;
    const int64_t selectionEnd = 100000;

    viewState.sampleOffset = 700;
    viewState.samplesPerPixel = 450.60443;

    auto *window = state.mainDocumentSessionWindow->getWindow();
    window->renderFrame();

    session.selection.setValue1(static_cast<double>(selectionStart));
    session.selection.setValue2(static_cast<double>(selectionEnd));
    cupuacu::actions::audio::performAmplifyFade(&state, 100.0, 0.0, 0);
    window->renderFrameIfDirty();

    auto surface = readWindowCanvas(window);
    REQUIRE(surface != nullptr);

    int32_t startEdge = 0;
    int32_t endEdge = 0;
    const bool hasFillEdges =
        cupuacu::gui::Waveform::computeBlockModeSelectionFillEdgePixels(
            selectionStart, selectionEnd, viewState.sampleOffset,
            viewState.samplesPerPixel, waveformBounds.w, startEdge, endEdge);
    const bool hasExactEdges =
        hasFillEdges ||
        cupuacu::gui::Waveform::computeBlockModeSelectionEdgePixels(
            selectionStart, selectionEnd, viewState.sampleOffset,
            viewState.samplesPerPixel, waveformBounds.w, startEdge, endEdge, 1,
            false);
    REQUIRE(hasExactEdges);

    const int insideHeight = computeWaveformColumnHeight(
        surface.get(), waveformBounds, waveformBounds.x + endEdge - 1);
    const int outsideHeight = computeWaveformColumnHeight(
        surface.get(), waveformBounds, waveformBounds.x + endEdge);

    INFO("insideHeight=" << insideHeight << " outsideHeight=" << outsideHeight
                         << " edges=[" << startEdge << "," << endEdge
                         << ") sampleOffset=" << viewState.sampleOffset
                         << " spp=" << viewState.samplesPerPixel);
    REQUIRE(outsideHeight > 0);
    REQUIRE(insideHeight < outsideHeight / 2);
}

TEST_CASE("Waveform smooth render helpers plan input buffers and quads", "[gui]")
{
    const auto input = cupuacu::gui::planWaveformSmoothRenderInput(
        3, 0.5, 2, 1.0, 10,
        [](const int64_t sampleIndex)
        {
            return static_cast<float>(sampleIndex);
        });

    REQUIRE(input.sampleX.size() == 4);
    REQUIRE(input.sampleY.size() == 4);
    REQUIRE(input.queryX.size() == 4);
    REQUIRE(input.sampleX[0] == Catch::Approx(-1.0));
    REQUIRE(input.sampleX[1] == Catch::Approx(1.0));
    REQUIRE(input.sampleY[0] == Catch::Approx(1.0));
    REQUIRE(input.sampleY[1] == Catch::Approx(2.0));
    REQUIRE(input.queryX[3] == Catch::Approx(3.0));

    const cupuacu::gui::WaveformSmoothRenderInput curvedInput{
        {-1.0, 1.0, 3.0, 5.0},
        {0.0, 1.0, 0.0, 1.0},
        {0.0, 1.0, 2.0, 3.0, 4.0}};
    const auto smoothed =
        cupuacu::gui::evaluateWaveformSmoothSpline(curvedInput);
    REQUIRE(smoothed.size() == curvedInput.queryX.size());
    REQUIRE(smoothed[0] == Catch::Approx(0.75f));
    REQUIRE(smoothed[1] == Catch::Approx(1.0f));
    REQUIRE(smoothed[2] == Catch::Approx(0.5f));
    REQUIRE(smoothed[4] == Catch::Approx(0.25f));

    const auto quad =
        cupuacu::gui::planWaveformSmoothSegmentQuad(0.0f, 2.0f, 0.0f, 0.0f, 1.0f);
    REQUIRE(quad.has_value());
    REQUIRE((*quad).vertices[0].x == Catch::Approx(0.0f));
    REQUIRE((*quad).vertices[0].y == Catch::Approx(-0.5f));
    REQUIRE((*quad).vertices[2].x == Catch::Approx(2.0f));
    REQUIRE((*quad).vertices[2].y == Catch::Approx(0.5f));

    REQUIRE_FALSE(cupuacu::gui::planWaveformSmoothSegmentQuad(
                      1.0f, 1.0f, 2.0f, 2.0f, 1.0f)
                      .has_value());
}

TEST_CASE("Sample point interaction planning clamps drag and sample value",
          "[gui]")
{
    const auto drag = cupuacu::gui::planSamplePointDrag(5.0f, -20.0f, 10, 30, 1.0);
    REQUIRE(drag.clampedY == Catch::Approx(0.0f));
    REQUIRE(drag.sampleValue == Catch::Approx(1.0f));

    const auto lower = cupuacu::gui::planSamplePointDrag(15.0f, 20.0f, 10, 30, 1.0);
    REQUIRE(lower.clampedY == Catch::Approx(20.0f));
    REQUIRE(lower.sampleValue == Catch::Approx(-1.0f));
}
