#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "actions/Play.hpp"
#include "audio/AudioDevices.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/DocumentSessionWindow.hpp"
#include "gui/MainView.hpp"
#include "gui/WaveformsUnderlay.hpp"

#include <vector>

TEST_CASE("Playback follow does not mutate DocumentSession cursor", "[session]")
{
    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 64);
    session.selection.reset();
    session.cursor = 10;

    state.audioDevices = std::make_shared<cupuacu::audio::AudioDevices>(false);
    state.mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            &state, &session, "test", 800, 400, SDL_WINDOW_HIDDEN);

    cupuacu::gui::MainView mainView(&state);
    state.mainView = &mainView;
    mainView.setBounds(0, 0, 800, 300);

    cupuacu::actions::play(&state);

    std::vector<float> output(8, 0.0f);
    state.audioDevices->processCallbackCycle(nullptr, output.data(), 4);
    mainView.timerCallback();

    REQUIRE(state.audioDevices->isPlaying());
    REQUIRE(state.audioDevices->getPlaybackPosition() == 14);
    REQUIRE(session.cursor == 10);
}

TEST_CASE("Playback range update while selecting matches release/end rules",
          "[session]")
{
    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 100);
    session.selection.setValue1(10.0);
    session.selection.setValue2(31.0); // R1 => [10, 31)
    session.cursor = 0;
    state.loopPlaybackEnabled = false;

    state.audioDevices = std::make_shared<cupuacu::audio::AudioDevices>(false);
    state.mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            &state, &session, "test", 800, 400, SDL_WINDOW_HIDDEN);

    cupuacu::gui::MainView mainView(&state);
    state.mainView = &mainView;
    mainView.setBounds(0, 0, 800, 300);

    std::vector<float> output(64, 0.0f);
    auto *window = state.mainDocumentSessionWindow->getWindow();

    SECTION("While dragging, keep old end; after release with new end before pos, still keep old end")
    {
        cupuacu::actions::play(&state);
        state.audioDevices->processCallbackCycle(nullptr, output.data(), 4); // pos 14
        REQUIRE(state.audioDevices->getPlaybackPosition() == 14);

        session.selection.setValue1(12.0);
        session.selection.setValue2(21.0); // R2 => end 21 (>14 now)
        cupuacu::gui::WaveformsUnderlay draggingSentinel(&state);
        window->setCapturingComponent(&draggingSentinel);
        mainView.timerCallback(); // must not apply while dragging

        state.audioDevices->processCallbackCycle(nullptr, output.data(), 8); // pos 22
        REQUIRE(state.audioDevices->isPlaying());
        REQUIRE(state.audioDevices->getPlaybackPosition() == 22);

        window->setCapturingComponent(nullptr);
        mainView.timerCallback(); // now applies, but 21 < current 22 => keep R1 end

        state.audioDevices->processCallbackCycle(nullptr, output.data(), 1);
        REQUIRE(state.audioDevices->isPlaying()); // would stop immediately if end switched to 21

        state.audioDevices->processCallbackCycle(nullptr, output.data(), 16);
        REQUIRE_FALSE(state.audioDevices->isPlaying());
    }

    SECTION("After release with new end after playback pos, switch to new end")
    {
        cupuacu::actions::play(&state);
        state.audioDevices->processCallbackCycle(nullptr, output.data(), 4); // pos 14
        REQUIRE(state.audioDevices->getPlaybackPosition() == 14);

        session.selection.setValue1(12.0);
        session.selection.setValue2(26.0); // R2 => end 26 (>14)
        window->setCapturingComponent(nullptr);
        mainView.timerCallback();

        state.audioDevices->processCallbackCycle(nullptr, output.data(), 12); // pos 26
        REQUIRE(state.audioDevices->isPlaying());
        state.audioDevices->processCallbackCycle(nullptr, output.data(), 1);
        REQUIRE_FALSE(state.audioDevices->isPlaying()); // stops at new end, before old R1 end
    }
}

TEST_CASE("Loop playback range update while selecting matches release/end rules",
          "[session]")
{
    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 100);
    session.selection.setValue1(10.0);
    session.selection.setValue2(31.0); // R1 => [10, 31)
    session.cursor = 0;
    state.loopPlaybackEnabled = true;

    state.audioDevices = std::make_shared<cupuacu::audio::AudioDevices>(false);
    state.mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            &state, &session, "test", 800, 400, SDL_WINDOW_HIDDEN);

    cupuacu::gui::MainView mainView(&state);
    state.mainView = &mainView;
    mainView.setBounds(0, 0, 800, 300);

    std::vector<float> output(64, 0.0f);
    auto *window = state.mainDocumentSessionWindow->getWindow();

    SECTION("While dragging, keep old loop end/start")
    {
        cupuacu::actions::play(&state);
        state.audioDevices->processCallbackCycle(nullptr, output.data(), 4); // pos 14
        REQUIRE(state.audioDevices->getPlaybackPosition() == 14);
        const int64_t oldStart = session.selection.getStartInt();
        const int64_t oldEndExclusive = session.selection.getEndInt() + 1;

        session.selection.setValue1(40.0);
        session.selection.setValue2(51.0); // R2 => [40, 51)
        cupuacu::gui::WaveformsUnderlay draggingSentinel(&state);
        window->setCapturingComponent(&draggingSentinel);
        mainView.timerCallback(); // must not apply while dragging

        const int64_t framesToLoopSample =
            (oldEndExclusive - state.audioDevices->getPlaybackPosition()) + 1;
        REQUIRE(framesToLoopSample > 0);
        state.audioDevices->processCallbackCycle(
            nullptr, output.data(), static_cast<unsigned long>(framesToLoopSample));
        REQUIRE(state.audioDevices->isPlaying());
        REQUIRE(state.audioDevices->getPlaybackPosition() == oldStart + 1);
    }

    SECTION("After release with new end before playback pos, finish old end then loop to new start")
    {
        cupuacu::actions::play(&state);
        state.audioDevices->processCallbackCycle(nullptr, output.data(), 12); // pos 22
        REQUIRE(state.audioDevices->getPlaybackPosition() == 22);
        const int64_t oldEndExclusive = session.selection.getEndInt() + 1;

        session.selection.setValue1(12.0);
        session.selection.setValue2(21.0); // R2 => [12, 21), end before pos
        const int64_t newStart = session.selection.getStartInt();
        window->setCapturingComponent(nullptr);
        mainView.timerCallback(); // apply

        const int64_t framesBeforeOldEnd =
            (oldEndExclusive - state.audioDevices->getPlaybackPosition()) - 1;
        REQUIRE(framesBeforeOldEnd >= 0);
        state.audioDevices->processCallbackCycle(
            nullptr, output.data(), static_cast<unsigned long>(framesBeforeOldEnd));
        REQUIRE(state.audioDevices->getPlaybackPosition() == oldEndExclusive - 1);
        state.audioDevices->processCallbackCycle(nullptr, output.data(), 1); // hit old end
        REQUIRE(state.audioDevices->getPlaybackPosition() == oldEndExclusive);
        state.audioDevices->processCallbackCycle(nullptr, output.data(), 1); // loop + emit start
        REQUIRE(state.audioDevices->isPlaying());
        REQUIRE(state.audioDevices->getPlaybackPosition() == newStart + 1);
    }

    SECTION("After release with new end after playback pos, use new end then loop to new start")
    {
        cupuacu::actions::play(&state);
        state.audioDevices->processCallbackCycle(nullptr, output.data(), 12); // pos 22
        REQUIRE(state.audioDevices->getPlaybackPosition() == 22);

        session.selection.setValue1(12.0);
        session.selection.setValue2(27.0); // R2 => [12, 27), end after pos
        const int64_t newStart = session.selection.getStartInt();
        const int64_t newEndExclusive = session.selection.getEndInt() + 1;
        window->setCapturingComponent(nullptr);
        mainView.timerCallback(); // apply

        const int64_t framesBeforeNewEnd =
            (newEndExclusive - state.audioDevices->getPlaybackPosition()) - 1;
        REQUIRE(framesBeforeNewEnd >= 0);
        state.audioDevices->processCallbackCycle(
            nullptr, output.data(), static_cast<unsigned long>(framesBeforeNewEnd));
        REQUIRE(state.audioDevices->getPlaybackPosition() == newEndExclusive - 1);
        state.audioDevices->processCallbackCycle(nullptr, output.data(), 1); // hit new end
        REQUIRE(state.audioDevices->getPlaybackPosition() == newEndExclusive);
        state.audioDevices->processCallbackCycle(nullptr, output.data(), 1); // loop + emit start
        REQUIRE(state.audioDevices->isPlaying());
        REQUIRE(state.audioDevices->getPlaybackPosition() == newStart + 1);
    }
}
