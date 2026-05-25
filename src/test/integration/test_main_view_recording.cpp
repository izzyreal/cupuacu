#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "IntegrationTestHelpers.hpp"

#include "State.hpp"
#include "actions/Play.hpp"
#include "actions/Record.hpp"
#include "gui/DevicePropertiesWindow.hpp"

#if defined(__APPLE__)
#include "platform/macos/MicrophonePermission.hpp"
#endif

#include <cmath>
#include <vector>

using Catch::Approx;

TEST_CASE("MainView integration consumes recorded chunks into the document",
          "[integration]")
{
#if defined(__APPLE__)
    struct MicrophonePermissionReset
    {
        ~MicrophonePermissionReset()
        {
            cupuacu::platform::macos::resetMicrophoneAccessOverrideForTesting();
        }
    } microphonePermissionReset;
    cupuacu::platform::macos::setMicrophoneAccessOverrideForTesting(true);
#endif
    cupuacu::test::StateWithTestPaths state{};
    auto ui = cupuacu::test::integration::createSessionUi(&state, 4, true, 2);
    auto &session = state.getActiveDocumentSession();
    auto &doc = session.document;

    for (int64_t i = 0; i < 4; ++i)
    {
        doc.setSample(0, i, static_cast<float>(i), false);
        doc.setSample(1, i, -static_cast<float>(i), false);
    }

    session.cursor = 4;
    cupuacu::actions::record(&state);
    state.audioDevices->drainQueue();
    REQUIRE(state.audioDevices->isRecording());

    const std::vector<float> input = {10.f, -10.f, 11.f, -11.f, 12.f, -12.f};
    state.audioDevices->processCallbackCycle(input.data(), nullptr, 3);

    ui.mainView->timerCallback();

    REQUIRE(doc.getFrameCount() == 7);
    REQUIRE(doc.getSample(0, 4) == Approx(10.f));
    REQUIRE(doc.getSample(1, 4) == Approx(-10.f));
    REQUIRE(doc.getSample(0, 6) == Approx(12.f));
    REQUIRE(doc.getSample(1, 6) == Approx(-12.f));
    REQUIRE(session.cursor == 7);
    REQUIRE(state.getActiveUndoables().empty());
}

TEST_CASE("MainView integration finalizes recording into an undoable when stopped",
          "[integration]")
{
#if defined(__APPLE__)
    struct MicrophonePermissionReset
    {
        ~MicrophonePermissionReset()
        {
            cupuacu::platform::macos::resetMicrophoneAccessOverrideForTesting();
        }
    } microphonePermissionReset;
    cupuacu::platform::macos::setMicrophoneAccessOverrideForTesting(true);
#endif
    cupuacu::test::StateWithTestPaths state{};
    auto ui = cupuacu::test::integration::createSessionUi(&state, 4, true, 2);
    auto &session = state.getActiveDocumentSession();
    auto &doc = session.document;

    for (int64_t i = 0; i < 4; ++i)
    {
        doc.setSample(0, i, static_cast<float>(i), false);
        doc.setSample(1, i, -static_cast<float>(i), false);
    }

    session.cursor = 2;
    cupuacu::actions::record(&state);
    state.audioDevices->drainQueue();
    REQUIRE(state.audioDevices->isRecording());

    const std::vector<float> input = {100.f, -100.f, 101.f, -101.f};
    state.audioDevices->processCallbackCycle(input.data(), nullptr, 2);
    ui.mainView->timerCallback();
    REQUIRE(state.getActiveUndoables().empty());

    cupuacu::actions::requestStop(&state);
    state.audioDevices->drainQueue();
    REQUIRE_FALSE(state.audioDevices->isRecording());

    ui.mainView->timerCallback();

    REQUIRE(state.getActiveUndoables().size() == 1);
    REQUIRE(state.getUndoDescription() == "Record");

    state.undo();
    REQUIRE(doc.getFrameCount() == 4);
    REQUIRE(doc.getSample(0, 2) == Approx(2.f));
    REQUIRE(doc.getSample(1, 2) == Approx(-2.f));
    REQUIRE(doc.getSample(0, 3) == Approx(3.f));
    REQUIRE(doc.getSample(1, 3) == Approx(-3.f));
    REQUIRE(session.cursor == 2);

    state.redo();
    REQUIRE(doc.getSample(0, 2) == Approx(100.f));
    REQUIRE(doc.getSample(1, 2) == Approx(-100.f));
    REQUIRE(doc.getSample(0, 3) == Approx(101.f));
    REQUIRE(doc.getSample(1, 3) == Approx(-101.f));
    REQUIRE(session.cursor == 4);
}

TEST_CASE(
    "MainView integration marks the window dirty when recording starts into an empty document",
    "[integration]")
{
#if defined(__APPLE__)
    struct MicrophonePermissionReset
    {
        ~MicrophonePermissionReset()
        {
            cupuacu::platform::macos::resetMicrophoneAccessOverrideForTesting();
        }
    } microphonePermissionReset;
    cupuacu::platform::macos::setMicrophoneAccessOverrideForTesting(true);
#endif
    cupuacu::test::StateWithTestPaths state{};
    auto ui = cupuacu::test::integration::createSessionUi(
        &state, 0, true, 2, 44100, 96, 240, 180);
    auto &session = state.getActiveDocumentSession();
    auto &doc = session.document;
    auto *window = state.mainDocumentSessionWindow->getWindow();
    REQUIRE(window != nullptr);

    window->renderFrame();
    window->getDirtyRects().clear();

    cupuacu::actions::record(&state);
    state.audioDevices->drainQueue();
    REQUIRE(state.audioDevices->isRecording());

    const std::vector<float> input = {10.f, -10.f, 11.f, -11.f, 12.f, -12.f};
    state.audioDevices->processCallbackCycle(input.data(), nullptr, 3);

    ui.mainView->timerCallback();

    REQUIRE(doc.getFrameCount() == 3);
    REQUIRE(doc.getSample(0, 0) == Approx(10.f));
    REQUIRE(doc.getSample(1, 2) == Approx(-12.f));
    REQUIRE(state.waveforms.size() == 2);
    REQUIRE_FALSE(window->getDirtyRects().empty());
}

TEST_CASE(
    "MainView integration auto-fits recording into an initially empty document and clamps zoom growth",
    "[integration]")
{
#if defined(__APPLE__)
    struct MicrophonePermissionReset
    {
        ~MicrophonePermissionReset()
        {
            cupuacu::platform::macos::resetMicrophoneAccessOverrideForTesting();
        }
    } microphonePermissionReset;
    cupuacu::platform::macos::setMicrophoneAccessOverrideForTesting(true);
#endif
    cupuacu::test::StateWithTestPaths state{};
    auto ui = cupuacu::test::integration::createSessionUi(&state, 0, true, 2);
    auto &session = state.getActiveDocumentSession();

    REQUIRE_FALSE(state.waveforms.empty());
    const int waveformWidth = state.waveforms.front()->getWidth();
    REQUIRE(waveformWidth > 0);

    cupuacu::actions::record(&state);
    state.audioDevices->drainQueue();
    REQUIRE(state.audioDevices->isRecording());

    const std::vector<float> firstChunk = {10.f, -10.f, 11.f, -11.f, 12.f, -12.f};
    state.audioDevices->processCallbackCycle(firstChunk.data(), nullptr, 3);
    ui.mainView->timerCallback();

    REQUIRE(session.document.getFrameCount() == 3);
    REQUIRE(state.getActiveViewState().samplesPerPixel ==
            Approx(3.0 / static_cast<double>(waveformWidth)));
    REQUIRE(state.getActiveViewState().sampleOffset == 0);

    std::vector<float> fillChunk(static_cast<std::size_t>(waveformWidth) * 2U, 0.25f);
    state.audioDevices->processCallbackCycle(fillChunk.data(), nullptr,
                                             static_cast<uint32_t>(waveformWidth / 2));
    ui.mainView->timerCallback();

    REQUIRE(session.document.getFrameCount() ==
            3 + static_cast<int64_t>(waveformWidth / 2));
    REQUIRE(state.getActiveViewState().samplesPerPixel ==
            Approx(static_cast<double>(session.document.getFrameCount()) /
                   static_cast<double>(waveformWidth)));
    REQUIRE(state.getActiveViewState().sampleOffset == 0);

    const uint32_t framesPerCycle =
        static_cast<uint32_t>(waveformWidth * 64);
    std::vector<float> longChunk(static_cast<std::size_t>(framesPerCycle) * 2U,
                                 0.5f);
    const int64_t framesNeededForClamp =
        static_cast<int64_t>(std::ceil(500.0 * static_cast<double>(waveformWidth)));
    const int64_t remainingFramesToClamp =
        std::max<int64_t>(0, framesNeededForClamp - session.document.getFrameCount());
    const int maxIterations =
        static_cast<int>(remainingFramesToClamp / framesPerCycle) + 2;
    for (int iteration = 0; iteration < maxIterations; ++iteration)
    {
        state.audioDevices->processCallbackCycle(longChunk.data(), nullptr,
                                                 framesPerCycle);
        ui.mainView->timerCallback();
        if (state.getActiveViewState().samplesPerPixel >= 500.0)
        {
            break;
        }
    }

    REQUIRE(state.getActiveViewState().samplesPerPixel == Approx(500.0));
}
