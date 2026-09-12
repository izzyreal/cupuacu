#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "IntegrationTestHelpers.hpp"

#include "State.hpp"
#include "actions/Play.hpp"
#include "actions/Record.hpp"
#include "actions/audio/RevisionRecording.hpp"
#include "audio/RecordedChunk.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/Waveform.hpp"
#include <catch2/generators/catch_generators.hpp>

#if defined(__APPLE__)
#include "platform/macos/MicrophonePermission.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <vector>

using Catch::Approx;

namespace
{
    template <class Predicate> void waitForRecordingRender(Predicate predicate)
    {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!predicate())
        {
            REQUIRE(std::chrono::steady_clock::now() < deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    struct WaveformReadGate
    {
        std::atomic<bool> released{false};
        void wait() const
        {
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!released)
            {
                if (std::chrono::steady_clock::now() >= deadline)
                {
                    throw std::runtime_error("Waveform read gate timed out");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    };

    struct HeldWaveformReads
    {
        std::vector<std::shared_ptr<WaveformReadGate>> gates;
        ~HeldWaveformReads()
        {
            for (auto &gate : gates)
            {
                gate->released = true;
            }
        }
        void hold(cupuacu::DocumentSession &session)
        {
            struct Reader : cupuacu::storage::AudioReader
            {
                std::shared_ptr<const AudioReader> original;
                std::shared_ptr<WaveformReadGate> gate;
                cupuacu::storage::AudioShape shape() const override
                {
                    return original->shape();
                }
                void readChannel(int channel, int64_t start,
                                 std::span<float> destination) const override
                {
                    gate->wait();
                    original->readChannel(channel, start, destination);
                }
            };
            auto gate = std::make_shared<WaveformReadGate>();
            gates.push_back(gate);
            // The session constructs a mutable source. Install the gate before
            // any waveform worker captures this newly committed revision.
            auto source =
                std::const_pointer_cast<cupuacu::waveform::ViewportSource>(
                    session.getViewportSource());
            auto reader = std::make_shared<Reader>();
            reader->original = source->audio;
            reader->gate = gate;
            source->audio = reader;
            source->prepare =
                [gate, prepare = source->prepare](const auto &cancel)
            {
                gate->wait();
                return !cancel() && (!prepare || prepare(cancel));
            };
        }
    };
} // namespace

TEST_CASE(
    "Recording waveform retains complete pixels while replacement reads are "
    "pending",
    "[integration][recording-waveform]")
{
    using namespace cupuacu;
    const int channels = GENERATE(1, 2);
    const double samplesPerPixel = GENERATE(0.25, 4.0, 128.0);
    CAPTURE(channels, samplesPerPixel);
    test::StateWithTestPaths state{};
    auto ui =
        test::integration::createSessionUi(&state, 262144, false, channels);
    auto &session = state.getActiveDocumentSession();
    session.bindReadRevision(storage::AudioEditRevision::silence(
        {262144, channels, 44100, SampleFormat::FLOAT32}));
    state.getActiveViewState().samplesPerPixel = samplesPerPixel;
    for (auto *waveform : state.waveforms)
    {
        waveform->setBounds(0, 0, 128, 80);
    }
    auto *renderer =
        state.mainDocumentSessionWindow->getWindow()->getRenderer();
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> target(
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                          SDL_TEXTUREACCESS_TARGET, 128, 80),
        SDL_DestroyTexture);
    REQUIRE(target);
    auto pixels = [&](gui::Waveform *waveform)
    {
        // Keep the moving recording cursor outside the measured waveform.
        session.cursor = session.document.getFrameCount();
        SDL_SetRenderTarget(renderer, target.get());
        SDL_SetRenderViewport(renderer, nullptr);
        waveform->onDraw(renderer);
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
            SDL_RenderReadPixels(renderer, nullptr), SDL_DestroySurface);
        REQUIRE(surface);
        std::vector<bool> green;
        for (int y = 0; y < surface->h; ++y)
        {
            for (int x = 0; x < surface->w; ++x)
            {
                Uint8 r, g, b, a;
                REQUIRE(
                    SDL_ReadSurfacePixel(surface.get(), x, y, &r, &g, &b, &a));
                green.push_back(g > 100 && r < 20 && b < 20);
            }
        }
        SDL_SetRenderTarget(renderer, nullptr);
        return green;
    };
    auto waitReady = [&]
    {
        waitForRecordingRender(
            [&]
            {
                bool ready = true;
                for (auto *waveform : state.waveforms)
                {
                    pixels(waveform);
                    ready = ready && waveform->isCurrentViewTextureReady();
                }
                return ready;
            });
    };
    waitReady();
    std::vector<std::vector<bool>> original;
    for (auto *waveform : state.waveforms)
    {
        original.push_back(pixels(waveform));
        REQUIRE(std::count(original.back().begin(), original.back().end(),
                           true) > 0);
    }
    actions::startRevisionRecording(
        &state, 0, test::makeUniqueTestRoot("recording-waveform") / "audio");
    auto recording = state.revisionRecording;
    HeldWaveformReads held;
    constexpr auto batchFrames = storage::RecordingWriter::batchFrames;
    for (int update = 0; update < 3; ++update)
    {
        for (int64_t offset = 0; offset < batchFrames; offset += 256)
        {
            audio::RecordedChunk chunk{
                update * batchFrames + offset, 256, uint8_t(channels), {}};
            std::fill(chunk.interleavedSamples.begin(),
                      chunk.interleavedSamples.end(), -0.5f);
            REQUIRE(recording->writer.submit(chunk));
        }
        waitForRecordingRender(
            [&]
            {
                return recording->writer.snapshot().endFrame >=
                       (update + 1) * batchFrames;
            });
        REQUIRE(actions::pollRevisionRecording(&state));
        held.hold(session);
        for (int channel = 0; channel < channels; ++channel)
        {
            REQUIRE((pixels(state.waveforms[channel]) == original[channel]));
            REQUIRE_FALSE(
                state.waveforms[channel]->isCurrentViewTextureReady());
        }
    }
    recording->finishing = true;
    recording->writer.finish();
    waitForRecordingRender(
        [&]
        {
            actions::pollRevisionRecording(&state);
            return !state.revisionRecording;
        });
    for (int channel = 0; channel < channels; ++channel)
    {
        REQUIRE((pixels(state.waveforms[channel]) == original[channel]));
    }

    SECTION("Completing the latest read replaces the retained pixels")
    {
        held.gates.back()->released = true;
        waitReady();
        for (int channel = 0; channel < channels; ++channel)
        {
            REQUIRE((pixels(state.waveforms[channel]) != original[channel]));
        }
    }
    SECTION("An unrelated invalidation clears the retained pixels")
    {
        gui::Waveform::invalidateAllRenderingCaches(&state);
        for (auto *waveform : state.waveforms)
        {
            const auto pending = pixels(waveform);
            REQUIRE(std::count(pending.begin(), pending.end(), true) == 0);
        }
    }
    SECTION(
        "Sample inspection reads the current recording while pixels are "
        "retained")
    {
        for (int channel = 0; channel < channels; ++channel)
        {
            auto *waveform = state.waveforms[channel];
            REQUIRE_FALSE(waveform->requestSampleValue(0));
            waitForRecordingRender(
                [&]
                {
                    waveform->timerCallback();
                    return waveform->requestSampleValue(0).has_value();
                });
            REQUIRE(*waveform->requestSampleValue(0) == Approx(-0.5f));
            REQUIRE((pixels(waveform) == original[channel]));
            REQUIRE_FALSE(waveform->isCurrentViewTextureReady());
        }
    }
    SECTION("An incompatible zoom clears the retained pixels")
    {
        state.getActiveViewState().verticalZoom *= 2;
        for (auto *waveform : state.waveforms)
        {
            const auto pending = pixels(waveform);
            REQUIRE(std::count(pending.begin(), pending.end(), true) == 0);
        }
    }
    SECTION("A replacement revision cannot display retained recording pixels")
    {
        REQUIRE(session.commitEditRevision(
            session.getEditRevision(),
            storage::AudioEditRevision::silence(
                {262144, channels, 44100, SampleFormat::FLOAT32}),
            {}));
        held.hold(session);
        for (auto *waveform : state.waveforms)
        {
            const auto pending = pixels(waveform);
            REQUIRE(std::count(pending.begin(), pending.end(), true) == 0);
        }
    }
}

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
    state.audioDevices->setRecordingPreparationResultForTesting(true);
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
    state.audioDevices->setRecordingPreparationResultForTesting(true);
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
    state.audioDevices->setRecordingPreparationResultForTesting(true);
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
    state.audioDevices->setRecordingPreparationResultForTesting(true);
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
        static_cast<uint32_t>(cupuacu::audio::kRecordedChunkFrames);
    std::vector<float> longChunk(static_cast<std::size_t>(framesPerCycle) * 2U,
                                 0.5f);
    const int64_t framesNeededForClamp =
        static_cast<int64_t>(std::ceil(500.0 * static_cast<double>(waveformWidth)));
    const int64_t remainingFramesToClamp =
        std::max<int64_t>(0, framesNeededForClamp - session.document.getFrameCount());
    const int maxIterations = static_cast<int>(
                                  remainingFramesToClamp /
                                  std::max<int64_t>(int64_t{1}, framesPerCycle)) +
                              2;
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
