#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestSdlTtfGuard.hpp"
#include "actions/Zoom.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/DocumentSessionWindow.hpp"
#include "gui/Waveform.hpp"

#include <SDL3/SDL.h>

#include <memory>
#include <vector>

namespace
{
    struct ZoomHarness
    {
        cupuacu::State state;
        std::unique_ptr<cupuacu::gui::DocumentSessionWindow> sessionWindow;
        std::vector<std::unique_ptr<cupuacu::gui::Waveform>> ownedWaveforms;
    };

    std::unique_ptr<ZoomHarness> makeZoomHarness(const int64_t frameCount,
                                                 const int waveformWidth)
    {
        cupuacu::test::ensureSdlTtfInitialized();

        auto harness = std::make_unique<ZoomHarness>();
        harness->state.activeDocumentSession.document.initialize(
            cupuacu::SampleFormat::FLOAT32, 44100, 2, frameCount);
        harness->sessionWindow =
            std::make_unique<cupuacu::gui::DocumentSessionWindow>(
                &harness->state, &harness->state.activeDocumentSession,
                "zoom-harness", 640, 240, SDL_WINDOW_HIDDEN);
        harness->state.mainDocumentSessionWindow = std::move(harness->sessionWindow);

        for (int channel = 0; channel < 2; ++channel)
        {
            auto waveform =
                std::make_unique<cupuacu::gui::Waveform>(&harness->state,
                                                         static_cast<uint8_t>(channel));
            waveform->setVisible(true);
            waveform->setBounds(0, 0, waveformWidth, 60);
            harness->state.waveforms.push_back(waveform.get());
            harness->ownedWaveforms.push_back(std::move(waveform));
        }

        return harness;
    }
} // namespace

TEST_CASE("Reset zoom derives samples-per-pixel from waveform width",
          "[gui]")
{
    auto harness = makeZoomHarness(1000, 200);
    auto &viewState = harness->state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 123.0;
    viewState.verticalZoom = 4.5;
    viewState.sampleOffset = 77;
    viewState.sampleValueUnderMouseCursor.emplace(0.75f);

    cupuacu::actions::resetZoom(&harness->state);

    REQUIRE(viewState.samplesPerPixel == Catch::Approx(5.0));
    REQUIRE(viewState.verticalZoom == Catch::Approx(cupuacu::INITIAL_VERTICAL_ZOOM));
    REQUIRE(viewState.sampleOffset == 0);
    REQUIRE_FALSE(viewState.sampleValueUnderMouseCursor.has_value());
}

TEST_CASE("Horizontal zoom keeps the view centered and respects bounds",
          "[gui]")
{
    auto harness = makeZoomHarness(1000, 200);
    auto &viewState = harness->state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 4.0;
    viewState.sampleOffset = 100;

    REQUIRE(cupuacu::actions::tryZoomInHorizontally(&harness->state));
    REQUIRE(viewState.samplesPerPixel == Catch::Approx(2.0));
    REQUIRE(viewState.sampleOffset == 301);

    REQUIRE(cupuacu::actions::tryZoomOutHorizontally(&harness->state));
    REQUIRE(viewState.samplesPerPixel == Catch::Approx(4.0));
    REQUIRE(viewState.sampleOffset == 100);

    viewState.samplesPerPixel = 1.0 / 200.0;
    REQUIRE_FALSE(cupuacu::actions::tryZoomInHorizontally(&harness->state));
}

TEST_CASE("Zoom selection focuses the selected range and resets vertical zoom",
          "[gui]")
{
    auto harness = makeZoomHarness(1000, 250);
    auto &selection = harness->state.activeDocumentSession.selection;
    auto &viewState = harness->state.mainDocumentSessionWindow->getViewState();

    REQUIRE_FALSE(cupuacu::actions::tryZoomSelection(&harness->state));

    selection.setHighest(1000.0);
    selection.setValue1(100.0);
    selection.setValue2(300.0);
    viewState.verticalZoom = 3.0;

    REQUIRE(cupuacu::actions::tryZoomSelection(&harness->state));
    REQUIRE(viewState.verticalZoom == Catch::Approx(cupuacu::INITIAL_VERTICAL_ZOOM));
    REQUIRE(viewState.samplesPerPixel == Catch::Approx(200.0 / 250.0));
    REQUIRE(viewState.sampleOffset == 100);
}
