#include <catch2/catch_test_macros.hpp>

#include "DocumentSession.hpp"
#include "State.hpp"
#include "actions/audio/EditCommands.hpp"
#include "actions/Play.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/DocumentSessionWindow.hpp"
#include "gui/MainView.hpp"
#include "playback/PlaybackRange.hpp"

TEST_CASE("Playback helper computes initial play range", "[session]")
{
    cupuacu::DocumentSession session{};
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 100);

    SECTION("Selection wins")
    {
        session.cursor = 90;
        session.selection.setValue1(10.0);
        session.selection.setValue2(31.0); // [10, 31)
        const auto range =
            cupuacu::playback::computeRangeForPlay(session, false);
        REQUIRE(range.start == 10);
        REQUIRE(range.end == 31);
    }

    SECTION("No selection, no loop starts at cursor")
    {
        session.selection.reset();
        session.cursor = 25;
        const auto range =
            cupuacu::playback::computeRangeForPlay(session, false);
        REQUIRE(range.start == 25);
        REQUIRE(range.end == 100);
    }

    SECTION("No selection, loop starts at cursor")
    {
        session.selection.reset();
        session.cursor = 40;
        const auto range =
            cupuacu::playback::computeRangeForPlay(session, true);
        REQUIRE(range.start == 40);
        REQUIRE(range.end == 100);
    }
}

TEST_CASE("Playback helper computes live-update range", "[session]")
{
    cupuacu::DocumentSession session{};
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 120);

    SECTION("Selection overrides fallback")
    {
        session.selection.setValue1(12.0);
        session.selection.setValue2(24.0); // [12, 24)
        const auto range = cupuacu::playback::computeRangeForLiveUpdate(
            session, false, 1, 2);
        REQUIRE(range.start == 12);
        REQUIRE(range.end == 24);
    }

    SECTION("No selection and loop uses whole document")
    {
        session.selection.reset();
        const auto range = cupuacu::playback::computeRangeForLiveUpdate(
            session, true, 35, 80);
        REQUIRE(range.start == 0);
        REQUIRE(range.end == 120);
    }

    SECTION("No selection and no loop keeps fallback")
    {
        session.selection.reset();
        const auto range = cupuacu::playback::computeRangeForLiveUpdate(
            session, false, 30, 70);
        REQUIRE(range.start == 30);
        REQUIRE(range.end == 70);
    }
}

TEST_CASE("Edit command helpers derive selection and paste targets", "[session]")
{
    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 128);

    SECTION("selection target inactive")
    {
        session.selection.reset();
        const auto target = cupuacu::actions::audio::selectionTarget(&state);
        REQUIRE(target.start == 0);
        REQUIRE(target.length == 0);
    }

    SECTION("selection target active")
    {
        session.selection.setValue1(11.0);
        session.selection.setValue2(19.0); // [11, 19)
        const auto target = cupuacu::actions::audio::selectionTarget(&state);
        REQUIRE(target.start == 11);
        REQUIRE(target.length == 8);
    }

    SECTION("paste target without selection uses cursor")
    {
        session.selection.reset();
        session.cursor = 33;
        const auto target = cupuacu::actions::audio::pasteTarget(&state);
        REQUIRE(target.start == 33);
        REQUIRE(target.end == -1);
    }

    SECTION("paste target with selection uses selection bounds")
    {
        session.selection.setValue1(7.0);
        session.selection.setValue2(15.0); // [7, 15)
        const auto target = cupuacu::actions::audio::pasteTarget(&state);
        REQUIRE(target.start == 7);
        REQUIRE(target.end == 14);
    }
}

TEST_CASE("Paste after zoom reset refreshes sample points", "[session]")
{
    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 256);
    state.mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            &state, &session, "test", 800, 400, SDL_WINDOW_HIDDEN);

    cupuacu::gui::MainView mainView(&state);
    state.mainView = &mainView;
    mainView.setBounds(0, 0, 800, 300);

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 0.01; // sample points visible for pixelScale=1
    cupuacu::gui::Waveform::updateAllSamplePoints(&state);
    REQUIRE_FALSE(state.waveforms.empty());
    REQUIRE(state.waveforms[0]->getChildren().size() > 0);

    // Prepare clipboard for paste.
    state.clipboard.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 8);
    for (int64_t i = 0; i < 8; ++i)
    {
        state.clipboard.setSample(0, i, 0.1f, false);
        state.clipboard.setSample(1, i, -0.1f, false);
    }

    session.selection.reset();
    session.cursor = 16;
    cupuacu::actions::audio::performPaste(&state);

    REQUIRE(viewState.samplesPerPixel > 0.01);
    REQUIRE(state.waveforms[0]->getChildren().empty());
}

TEST_CASE("Sample points are hidden during playback", "[session]")
{
    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 256);
    state.audioDevices = std::make_shared<cupuacu::audio::AudioDevices>(false);
    state.mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            &state, &session, "test", 800, 400, SDL_WINDOW_HIDDEN);

    cupuacu::gui::MainView mainView(&state);
    state.mainView = &mainView;
    mainView.setBounds(0, 0, 800, 300);

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 0.01; // sample points visible for pixelScale=1
    cupuacu::gui::Waveform::updateAllSamplePoints(&state);
    REQUIRE_FALSE(state.waveforms.empty());
    REQUIRE(state.waveforms[0]->getChildren().size() > 0);

    cupuacu::actions::play(&state);
    std::vector<float> output(64, 0.0f);
    state.audioDevices->processCallbackCycle(nullptr, output.data(), 4);
    mainView.timerCallback();
    REQUIRE(state.audioDevices->isPlaying());
    REQUIRE(state.waveforms[0]->getChildren().empty());
}
