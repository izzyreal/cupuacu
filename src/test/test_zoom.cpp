#include <catch2/catch_test_macros.hpp>

#include "TestStateBuilders.hpp"
#include "actions/audio/EditCommands.hpp"
#include "actions/Zoom.hpp"
#include "gui/Waveform.hpp"

#include <cmath>

TEST_CASE("Paste keeps zoom level unchanged", "[session]")
{
    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    [[maybe_unused]] auto ui = cupuacu::test::createSessionUi(&state, 256);

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 0.01;
    cupuacu::gui::Waveform::updateAllSamplePoints(&state);
    REQUIRE_FALSE(state.waveforms.empty());
    REQUIRE(state.waveforms[0]->getChildren().size() > 0);

    state.clipboard.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 8);
    for (int64_t i = 0; i < 8; ++i)
    {
        state.clipboard.setSample(0, i, 0.1f, false);
        state.clipboard.setSample(1, i, -0.1f, false);
    }

    session.selection.reset();
    session.cursor = 16;
    const double zoomBefore = viewState.samplesPerPixel;
    cupuacu::actions::audio::performPaste(&state);
    REQUIRE(viewState.samplesPerPixel == zoomBefore);
}

TEST_CASE("Cut keeps zoom and offset when no right gap risk", "[session]")
{
    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    [[maybe_unused]] auto ui = cupuacu::test::createSessionUi(&state, 300);

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 0.25;
    updateSampleOffset(&state, 20);
    const int64_t offsetBefore = viewState.sampleOffset;
    REQUIRE(offsetBefore >= 0);
    const double zoomBefore = viewState.samplesPerPixel;

    session.selection.setValue1(10.0);
    session.selection.setValue2(20.0);
    cupuacu::actions::audio::performCut(&state);

    REQUIRE(viewState.samplesPerPixel == zoomBefore);
    REQUIRE(viewState.sampleOffset == offsetBefore);
}

TEST_CASE("Cut keeps zoom and clamps offset when right gap would appear", "[session]")
{
    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    [[maybe_unused]] auto ui = cupuacu::test::createSessionUi(&state, 300);

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 0.25;
    const int64_t oldMaxOffset = getMaxSampleOffset(&state);
    REQUIRE(oldMaxOffset > 0);
    updateSampleOffset(&state, oldMaxOffset);
    REQUIRE(viewState.sampleOffset == oldMaxOffset);
    const double zoomBefore = viewState.samplesPerPixel;

    session.selection.setValue1(0.0);
    session.selection.setValue2(1.0);
    cupuacu::actions::audio::performCut(&state);

    REQUIRE(viewState.samplesPerPixel == zoomBefore);
    REQUIRE(viewState.sampleOffset <= oldMaxOffset);
    REQUIRE(viewState.sampleOffset == getMaxSampleOffset(&state));
}

TEST_CASE("Sample offset clamp never goes negative when viewport exceeds document",
          "[session]")
{
    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    [[maybe_unused]] auto ui = cupuacu::test::createSessionUi(&state, 80);

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 10.0;

    updateSampleOffset(&state, 9999);
    REQUIRE(viewState.sampleOffset == 0);
}

TEST_CASE("Cut at fully zoomed-out view keeps sampleOffset at zero", "[session]")
{
    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    [[maybe_unused]] auto ui = cupuacu::test::createSessionUi(&state, 400);

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    const int64_t framesBefore = session.document.getFrameCount();
    cupuacu::actions::resetZoom(&state);
    const double zoomBefore = viewState.samplesPerPixel;
    updateSampleOffset(&state, 0);
    REQUIRE(viewState.sampleOffset == 0);

    session.selection.setValue1(200.0);
    session.selection.setValue2(400.0);
    cupuacu::actions::audio::performCut(&state);

    const int64_t framesAfter = session.document.getFrameCount();
    REQUIRE(framesAfter > 0);
    REQUIRE(viewState.sampleOffset == 0);
    REQUIRE(viewState.samplesPerPixel != zoomBefore);
    REQUIRE(viewState.samplesPerPixel <
            static_cast<double>(framesBefore) /
                static_cast<double>(cupuacu::gui::Waveform::getWaveformWidth(
                    &state)));
    const double expectedZoom =
        static_cast<double>(framesAfter) /
        static_cast<double>(cupuacu::gui::Waveform::getWaveformWidth(&state));
    REQUIRE(std::abs(viewState.samplesPerPixel - expectedZoom) < 1e-6);
}

TEST_CASE("Horizontal zoom-in keeps visible center from full-file view",
          "[session]")
{
    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    [[maybe_unused]] auto ui = cupuacu::test::createSessionUi(&state, 20000);

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    cupuacu::actions::resetZoom(&state);
    const int waveformWidth = cupuacu::gui::Waveform::getWaveformWidth(&state);
    REQUIRE(waveformWidth > 0);

    const double oldCenter =
        ((waveformWidth / 2.0 + 0.5) * viewState.samplesPerPixel) +
        viewState.sampleOffset;

    REQUIRE(cupuacu::actions::tryZoomInHorizontally(&state));

    const double newCenter =
        ((waveformWidth / 2.0 + 0.5) * viewState.samplesPerPixel) +
        viewState.sampleOffset;

    REQUIRE(std::abs(newCenter - oldCenter) <= 1.0);
    REQUIRE(viewState.sampleOffset > 0);
}

TEST_CASE("Horizontal zoom-in keeps visible center from arbitrary viewport",
          "[session]")
{
    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    [[maybe_unused]] auto ui = cupuacu::test::createSessionUi(&state, 20000);

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 4.0;
    updateSampleOffset(&state, 2500);

    const int waveformWidth = cupuacu::gui::Waveform::getWaveformWidth(&state);
    REQUIRE(waveformWidth > 0);

    const double oldCenter =
        ((waveformWidth / 2.0 + 0.5) * viewState.samplesPerPixel) +
        viewState.sampleOffset;

    REQUIRE(cupuacu::actions::tryZoomInHorizontally(&state));

    const double newCenter =
        ((waveformWidth / 2.0 + 0.5) * viewState.samplesPerPixel) +
        viewState.sampleOffset;

    REQUIRE(std::abs(newCenter - oldCenter) <= 1.0);
}
