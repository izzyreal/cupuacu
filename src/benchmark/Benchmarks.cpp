#include "ApplicationLoop.hpp"
#include "BenchmarkBuild.hpp"
#include "BenchmarkSourceFingerprint.hpp"
#include "BuildInfo.hpp"
#include "actions/audio/EditCommands.hpp"
#include "actions/audio/SetSampleValue.hpp"
#include "actions/Zoom.hpp"
#include "file/file_loading.hpp"
#include "gui/WaveformOverviewPlanning.hpp"
#include "performance/WorkMetrics.hpp"

#include <benchmark/benchmark.h>
#include <nlohmann/json.hpp>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#endif

namespace
{
    using Json = nlohmann::json;
    using Clock = std::chrono::steady_clock;
    using namespace cupuacu;
    constexpr int sampleRate = 48000;
    constexpr int channels = 2;
    constexpr int viewportWidth = 1024;
    Json request, result;

    void require(bool condition, const std::string &message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    double elapsed(Clock::time_point start)
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - start)
            .count();
    }

    // Integer mixing makes fixtures independent of libc random and
    // transcendental implementations. Values are exact multiples of 1/32768,
    // including silence.
    float sampleAt(int64_t frame, int channel)
    {
        if ((frame / 8192) % 7 == 0)
        {
            return 0;
        }
        if (frame % 4096 == 0)
        {
            return channel == 0 ? 0.875f : -0.875f;
        }
        uint32_t x = uint32_t(frame) ^ (uint32_t(channel + 1) * 0x9e3779b9u);
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        const int value = int(x & 32767u) - 16384;
        return float(value) / 32768.0f;
    }

    class BenchPaths final : public Paths
    {
    public:
        explicit BenchPaths(std::filesystem::path path) : root(std::move(path))
        {
        }

    protected:
        std::filesystem::path appConfigHome() const override
        {
            return root / "config";
        }
        std::filesystem::path appDocumentsPath() const override
        {
            return root / "documents";
        }
        std::filesystem::path appLogHome() const override
        {
            return root / "logs";
        }

    private:
        std::filesystem::path root;
    };

    void generateFixture(const std::filesystem::path &path, int64_t frames,
                         const std::string &format)
    {
        require(frames >= 2048, "Fixture must contain at least 2048 frames");
        std::filesystem::create_directories(path.parent_path());
        SF_INFO info{};
        info.channels = channels;
        info.samplerate = sampleRate;
        info.format = (format == "flac"  ? SF_FORMAT_FLAC
                       : format == "caf" ? SF_FORMAT_CAF
                                         : SF_FORMAT_WAV) |
                      SF_FORMAT_PCM_16;
        SNDFILE *file = file::openSndfile(path, SFM_WRITE, &info);
        require(file != nullptr, "Could not create fixture");
        std::vector<short> block(16384 * channels);
        for (int64_t pos = 0; pos < frames; pos += 16384)
        {
            const auto count = std::min<int64_t>(16384, frames - pos);
            for (int64_t i = 0; i < count; ++i)
            {
                for (int ch = 0; ch < channels; ++ch)
                {
                    block[i * channels + ch] =
                        short(sampleAt(pos + i, ch) * 32768.0f);
                }
            }
            if (sf_writef_short(file, block.data(), count) != count)
            {
                sf_close(file);
                throw std::runtime_error("Fixture write failed");
            }
        }
        require(sf_close(file) == 0, "Fixture close failed");
    }

    void initialize(DocumentSession &session, int64_t frames)
    {
        session.document.initialize(SampleFormat::PCM_S16, sampleRate, channels,
                                    frames);
        std::vector<float> block(16384 * channels);
        for (int64_t pos = 0; pos < frames; pos += 16384)
        {
            const auto count = std::min<int64_t>(16384, frames - pos);
            for (int64_t i = 0; i < count; ++i)
            {
                for (int ch = 0; ch < channels; ++ch)
                {
                    block[i * channels + ch] = sampleAt(pos + i, ch);
                }
            }
            session.document.writeInterleavedFloatBlock(pos, block.data(),
                                                        count, channels);
        }
        session.syncSelectionAndCursorToDocumentLength();
        session.waveformCaches.resetToChannelCount(channels);
        session.rebuildWaveformCacheSynchronously();
    }

    void setupWindow(State &state)
    {
#if CUPUACU_BENCHMARK_SDL
        auto &session = state.getActiveDocumentSession();
        state.mainDocumentSessionWindow =
            std::make_unique<gui::DocumentSessionWindow>(
                &state, &session, &state.getActiveViewState(),
                "Cupuacu benchmark", viewportWidth + 16, 500, SDL_WINDOW_HIDDEN);
        auto *window = state.mainDocumentSessionWindow->getWindow();
        require(window->isOpen(), SDL_GetError());
        state.windows.push_back(window);
        require(std::string(SDL_GetRendererName(window->getRenderer())) ==
                    "software",
                "SDL benchmark requires software rendering");
        gui::buildComponents(&state, window);
        // At Xvfb's 1x display scale the Linux layout has two 8-pixel borders.
        // Check the real layout, including any later rebuild after opening.
        for (auto *waveform : state.waveforms)
        {
            require(waveform->getWidth() == viewportWidth,
                    "SDL benchmark requires a 1024-pixel waveform viewport");
        }
        state.mainWindowInitialFrameRendered = true;
        window->renderFrame();
#else
        (void)state;
#endif
    }

    bool renderReady(const State &state)
    {
#if CUPUACU_BENCHMARK_SDL
        return !state.waveforms.empty() &&
               std::all_of(state.waveforms.begin(), state.waveforms.end(),
                           [](const auto *waveform)
                           {
                               require(waveform->getWidth() == viewportWidth,
                                       "Waveform viewport changed size");
                               return waveform->isCurrentViewTextureReady();
                           });
#else
        (void)state;
        return false;
#endif
    }

    void pump(State &state)
    {
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
#if CUPUACU_BENCHMARK_SDL
            gui::handleAppEvent(&state, &event);
#else
            if (state.eventObserver)
            {
                state.eventObserver(event);
            }
#endif
        }
        iterateApplication(&state);
#if !CUPUACU_BENCHMARK_SDL
        // SDL waveform timers consume cache updates and invalidate textures.
        // A second pump here would consume notifications before those timers.
        for (auto &tab : state.tabs)
        {
            (void)tab.session.pumpWaveformCacheWork(state.paths.get());
        }
#endif
    }

    bool busy(State &state)
    {
        if (state.backgroundEffectJob || state.backgroundOpenJob ||
            state.backgroundSaveJob || state.backgroundAutosaveJob ||
            state.pendingOpenWaveformBuild.active ||
            !state.pendingOpenFiles.empty() || state.longTask.active)
        {
            return true;
        }
        for (auto &tab : state.tabs)
        {
            if (tab.session.getWaveformCacheBuildProgress())
            {
                return true;
            }
        }
        return false;
    }

    void drain(State &state, bool waitForRender = false)
    {
        const auto deadline =
            Clock::now() +
            std::chrono::seconds(request.value("timeout_seconds", 120));
        do
        {
            require(Clock::now() < deadline, "Background work timed out");
            pump(state);
        } while (busy(state) || (waitForRender && !renderReady(state)));
    }

    class Probes
    {
    public:
        explicit Probes(State &stateToUse) : state(stateToUse)
        {
            type = SDL_RegisterEvents(1);
            require(type != Uint32(-1), "Could not register probe events");
            delays.reserve(4096);
            state.eventObserver = [this](const SDL_Event &event)
            {
                if (event.type == SDL_EVENT_KEY_DOWN)
                {
                    state.longTask.active ? ++blockedKeys : ++permittedKeys;
                }
                if (event.type == type)
                {
                    delays.push_back(
                        double(SDL_GetTicksNS() - event.user.timestamp) / 1e6);
                }
            };
            const Uint32 windowId =
                state.mainDocumentSessionWindow
                    ? state.mainDocumentSessionWindow->getWindow()->getId()
                    : 0;
            producer = std::jthread(
                [this, windowId](std::stop_token stop)
                {
                    auto next = Clock::now();
                    uint64_t nextTicks = SDL_GetTicksNS();
                    while (!stop.stop_requested())
                    {
                        SDL_Event event{};
                        event.type = type;
                        event.user.timestamp = nextTicks;
                        if (!SDL_PushEvent(&event))
                        {
                            ++dropped;
                        }
                        if (windowId != 0)
                        {
                            SDL_Event key{};
                            key.type = SDL_EVENT_KEY_DOWN;
                            key.key.windowID = windowId;
                            key.key.scancode = SDL_SCANCODE_RIGHT;
                            key.key.key = SDLK_RIGHT;
                            if (!SDL_PushEvent(&key))
                            {
                                ++dropped;
                            }
                        }
                        next += std::chrono::milliseconds(2);
                        nextTicks += 2000000;
                        std::this_thread::sleep_until(next);
                    }
                });
        }
        void finish()
        {
            producer.request_stop();
            producer.join();
            // May be consumed by the production long-task nested event pump
            // too.
            pump(state);
            std::sort(delays.begin(), delays.end());
            Json data{{"samples", delays.size()},
                      {"dropped", dropped.load()},
                      {"max_ms",
                       delays.empty() ? Json(nullptr) : Json(delays.back())},
                      {"p95_ms", nullptr},
                      {"p99_ms", nullptr}};
            if (delays.size() >= 100)
            {
                data["p95_ms"] =
                    delays[size_t(std::ceil(delays.size() * .95)) - 1];
            }
            if (delays.size() >= 1000)
            {
                data["p99_ms"] =
                    delays[size_t(std::ceil(delays.size() * .99)) - 1];
            }
            result["event_latency"] = data;
            result["navigation_during_work"] = {
                {"permitted_by_long_task_gate", permittedKeys},
                {"blocked_by_long_task_gate", blockedKeys}};
            state.eventObserver = {};
        }
        ~Probes()
        {
            if (producer.joinable())
            {
                producer.request_stop();
                producer.join();
            }
            state.eventObserver = {};
        }

    private:
        State &state;
        Uint32 type;
        std::jthread producer;
        std::atomic<unsigned> dropped{0};
        std::vector<double> delays;
        unsigned blockedKeys = 0, permittedKeys = 0;
    };

    void selection(State &state, int64_t start, int64_t length)
    {
        auto &session = state.getActiveDocumentSession();
        session.selection.setValue1(double(start));
        session.selection.setValue2(double(start + length));
        session.cursor = start;
    }

    void validateSamples(const Document &document, int64_t frames,
                         const std::function<float(int64_t, int)> &expected)
    {
        require(document.getFrameCount() == frames, "Frame count mismatch");
        require(document.getChannelCount() == channels,
                "Channel count mismatch");
        // Complete validation happens after counters/timers have been captured.
        const auto lease = document.acquireReadLease();
        for (int ch = 0; ch < channels; ++ch)
        {
            for (int64_t i = 0; i < frames; ++i)
            {
                if (std::abs(lease.getSample(ch, i) - expected(i, ch)) >=
                    0.000002f)
                {
                    throw std::runtime_error("Audio result mismatch at frame " +
                                             std::to_string(i));
                }
            }
        }
    }

    void captureMetrics()
    {
#if CUPUACU_WORK_METRICS
        for (unsigned i = 0; i < unsigned(performance::Work::Count); ++i)
        {
            result["work"][performance::names[i]] =
                performance::registry.work[i].load();
        }
        result["tracked_capacity_end_bytes"] =
            performance::registry.liveBytes.load();
        result["tracked_capacity_peak_bytes"] =
            performance::registry.peakBytes.load();
#else
        result["work"] = nullptr;
#endif
    }

    void navigation(State &state, const std::string &scenario, int64_t frames)
    {
        gui::WaveformOverviewDebugStats stats{};
        double checksum = 0;
        int accepted = 0, blocked = 0;
        for (int step = 0; step < 12; ++step)
        {
            auto &view = state.getActiveViewState();
            const bool zoom = scenario.find("zoom") != std::string::npos;
            double spp =
                zoom ? (step % 3 == 0   ? 0.5
                        : step % 3 == 1 ? 128.0
                                        : double(frames) / viewportWidth)
                     : 128.0;
            auto offset =
                int64_t((double(std::max<int64_t>(
                             0, frames - int64_t(spp * viewportWidth))) *
                         step) /
                        11);
            view.samplesPerPixel = spp;
            view.sampleOffset = offset;
#if CUPUACU_BENCHMARK_SDL
            // Establish the scenario's starting view, then issue the real
            // command.
            if (zoom)
            {
                view.samplesPerPixel = spp * 2.0;
            }
            SDL_Event key{};
            key.type = SDL_EVENT_KEY_DOWN;
            key.key.windowID =
                state.mainDocumentSessionWindow->getWindow()->getId();
            key.key.scancode = zoom ? SDL_SCANCODE_W : SDL_SCANCODE_RIGHT;
            key.key.key = zoom ? SDLK_W : SDLK_RIGHT;
            const bool wasBlocked = state.longTask.active;
            gui::handleAppEvent(&state, &key);
            wasBlocked ? ++blocked : ++accepted;
            spp = view.samplesPerPixel;
            offset = view.sampleOffset;
#endif
            for (int ch = 0; ch < channels; ++ch)
            {
                for (int x = 0; x < viewportWidth; ++x)
                {
                    gui::Peak peak{};
                    const double begin = offset + x * spp;
                    if (gui::computeWaveformPeakForSampleWindow(
                            state.getActiveDocumentSession(), ch, offset, spp,
                            1, begin, begin + spp, peak, &stats))
                    {
                        checksum += peak.min + peak.max;
                    }
                }
            }
#if CUPUACU_BENCHMARK_SDL
            gui::Waveform::updateAllSamplePoints(&state);
            gui::Waveform::setAllWaveformsDirty(&state);
            drain(state, true);
#else
            pump(state);
#endif
        }
        benchmark::DoNotOptimize(checksum);
        result["waveform_queries"] = {
            {"windows", stats.windowsRequested},
            {"raw_samples_scanned", stats.rawSamplesScanned},
            {"cached_peaks_used", stats.cachedPeaksUsed}};
        result["navigation_dispatch"] = {{"accepted", accepted},
                                         {"blocked", blocked}};
    }

    void scenario(benchmark::State &measurement)
    {
#if CUPUACU_WORK_METRICS
        // Validate an explicit deep copy without requiring editor commands to
        // keep cloning entire documents after storage implementations improve.
        {
            audio::AudioBuffer control;
            control.resize(2, 128);
            performance::resetWork();
            const auto copy = control.clone();
            require(
                performance::registry
                        .work[unsigned(performance::Work::SampleBytesCopied)]
                        .load() == 1024,
                "Deep-copy byte observation mismatch");
            require(copy->getFrameCount() == 128, "Deep-copy control failed");
        }
        // Shared pages are charged once, and snapshots must not copy peaks.
        const auto capacityBeforePeaks = performance::registry.liveBytes.load();
        {
            gui::PeakLevel peaks;
            peaks.resize(gui::PeakLevel::PEAKS_PER_PAGE + 7);
            peaks.set(0, {-1, 1});
            const auto originalCapacity =
                performance::registry.liveBytes.load();
            performance::resetWork();
            auto snapshot = peaks;
            require(performance::registry.liveBytes.load() == originalCapacity,
                    "Shared peak capacity was counted twice");
            require(performance::registry
                            .work[unsigned(performance::Work::PeakBytesCopied)]
                            .load() == 0,
                    "Peak snapshot unexpectedly copied data");
            snapshot.set(0, {-2, 2});
            require(peaks[0].max == 1 && snapshot[0].max == 2,
                    "Peak snapshot isolation failed");
            require(performance::registry
                            .work[unsigned(performance::Work::PeakBytesCopied)]
                            .load() ==
                        gui::PeakLevel::PEAKS_PER_PAGE * sizeof(gui::Peak),
                    "Peak page copy observation mismatch");
        }
        require(performance::registry.liveBytes.load() == capacityBeforePeaks,
                "Peak capacity was not released");
#endif
        const std::string name = request.at("scenario");
        const int64_t frames = request.at("frames");
        const bool opening = name.starts_with("open_");
        const bool navigating =
            name.starts_with("scroll") || name.starts_with("zoom");
        const bool latency = name.starts_with("responsive_");
        State state;
        state.paths =
            std::make_unique<BenchPaths>(request.at("root").get<std::string>());
        std::string error;
        state.errorReporter =
            [&error](const std::string &title, const std::string &detail)
        {
            error = title + ": " + detail;
        };
        if (!opening)
        {
            initialize(state.getActiveDocumentSession(), frames);
        }
        setupWindow(state);
        std::optional<Document> retained;
        int64_t start =
            request.value("position", "begin") == "middle" ? frames / 2
            : request.value("position", "begin") == "end"  ? frames - 1000
                                                           : 1000;
        int64_t expectedFrames = frames;
        std::function<float(int64_t, int)> expected = sampleAt;
        auto gain = [&state](int64_t count)
        {
            selection(state, 1000, count);
            require(actions::effects::queueAmplifyFade(
                        &state, effects::AmplifyFadeSettings{50, 50, 0, true}),
                    "Gain was not queued");
        };
        if (name == "sample_shared")
        {
            retained = state.getActiveDocumentSession().document;
        }
        if (name == "paste")
        {
            selection(state, 1000, 1000);
            actions::audio::performCopy(&state);
            drain(state);
            state.getActiveDocumentSession().selection.reset();
            state.getActiveDocumentSession().cursor = 2000;
        }
        if (name == "undo" || name == "redo")
        {
            selection(state, start, 1000);
            actions::audio::performDelete(&state);
            drain(state);
            if (name == "redo")
            {
                state.undo();
                drain(state);
            }
        }
        if (opening && name == "open_cached")
        {
            DocumentSession cached;
            auto loaded = file::loadAudioFile(request.at("fixture"));
            cached.document = std::move(loaded.document);
            cached.setCurrentFile(request.at("fixture"));
            cached.waveformCaches.resetToChannelCount(channels);
            cached.rebuildWaveformCacheSynchronously();
            require(waveform::savePersistentWaveformCache(cached, *state.paths),
                    "Peak fixture save failed");
        }
        if (navigating && name.ends_with("dirty"))
        {
            state.getActiveDocumentSession().document.removeFrames(start, 1000);
            state.getActiveDocumentSession().waveformCaches.applyErase(start,
                                                                       1000);
            expectedFrames -= 1000;
            expected = [start](int64_t i, int ch)
            {
                return sampleAt(i < start ? i : i + 1000, ch);
            };
        }
        else if (!opening)
        {
            drain(state, CUPUACU_BENCHMARK_SDL);
        }

        std::unique_ptr<Probes> probes;
        if (latency)
        {
            probes = std::make_unique<Probes>(state);
        }
        performance::resetWork();
        result["tracked_capacity_start_bytes"] =
            CUPUACU_WORK_METRICS ? Json(performance::registry.liveBytes.load())
                                 : Json(nullptr);
        for (auto iteration : measurement)
        {
            (void)iteration;
            const auto started = Clock::now();
            if (opening)
            {
                actions::io::queueOpenFile(&state, request.at("fixture"));
            }
            else if (navigating)
            {
                if (name.ends_with("dirty"))
                {
                    state.getActiveDocumentSession().updateWaveformCache();
                }
                navigation(state, name, expectedFrames);
            }
            else if (name == "sample" || name == "sample_shared")
            {
                auto edit = std::make_shared<actions::audio::SetSampleValue>(
                    &state, 0, 9001, sampleAt(9001, 0), 0.25f);
                state.addAndDoUndoable(edit);
                expected = [](int64_t i, int ch)
                {
                    return i == 9001 && ch == 0 ? 0.25f : sampleAt(i, ch);
                };
            }
            else if (name == "delete" || name == "redo")
            {
                if (name == "delete")
                {
                    selection(state, start, 1000);
                    actions::audio::performDelete(&state);
                }
                else
                {
                    state.redo();
                }
                expectedFrames -= 1000;
                expected = [start](int64_t i, int ch)
                {
                    return sampleAt(i < start ? i : i + 1000, ch);
                };
            }
            else if (name == "undo")
            {
                state.undo();
            }
            else if (name == "copy")
            {
                selection(state, 1000, 1000);
                actions::audio::performCopy(&state);
            }
            else if (name == "paste")
            {
                actions::audio::performPaste(&state);
                expectedFrames += 1000;
                expected = [](int64_t i, int ch)
                {
                    return sampleAt(i < 2000 ? i : i - 1000, ch);
                };
            }
            else if (name == "trim")
            {
                selection(state, 1000, 1000);
                actions::audio::performTrim(&state);
                expectedFrames = 1000;
                expected = [](int64_t i, int ch)
                {
                    return sampleAt(i + 1000, ch);
                };
            }
            else if (name == "gain_fixed" || name == "gain_all" ||
                     name == "responsive_gain" || name == "history")
            {
                const int depth =
                    name == "history" ? request.value("history_depth", 1) : 1;
                for (int i = 0; i < depth; ++i)
                {
                    if (name == "gain_all" || name == "responsive_gain")
                    {
                        state.getActiveDocumentSession().selection.reset();
                        require(
                            actions::effects::queueAmplifyFade(
                                &state,
                                effects::AmplifyFadeSettings{50, 50, 0, true}),
                            "Gain was not queued");
                    }
                    else
                    {
                        gain(1000);
                    }
                    if (i + 1 < depth)
                    {
                        drain(state);
                    }
                }
                const bool whole =
                    name == "gain_all" || name == "responsive_gain";
                expected = [depth, whole](int64_t i, int ch)
                {
                    return sampleAt(i, ch) * ((whole || (i >= 1000 && i < 2000))
                                                  ? std::ldexp(1.0f, -depth)
                                                  : 1.0f);
                };
            }
            else if (name == "responsive_stall")
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
            }
            else if (name == "waveform_build")
            {
                state.getActiveDocumentSession().invalidateWaveformSamples(
                    0, frames - 1);
                state.getActiveDocumentSession()
                    .rebuildWaveformCacheSynchronously();
            }
            else
            {
                throw std::runtime_error("Unknown scenario: " + name);
            }
            result["milestones_ms"]["command_return"] = elapsed(started);
            if (!opening && !state.backgroundEffectJob)
            {
                result["milestones_ms"]["committed"] = elapsed(started);
            }
            bool sawAudio = !opening;
            const auto deadline =
                Clock::now() +
                std::chrono::seconds(request.value("timeout_seconds", 120));
            do
            {
                require(Clock::now() < deadline, "Scenario timed out");
                pump(state);
                if (opening && !sawAudio &&
                    state.getActiveDocumentSession().document.getFrameCount() ==
                        frames)
                {
                    sawAudio = true;
                    result["milestones_ms"]["audio_available"] =
                        elapsed(started);
                    result["milestones_ms"]["committed"] = elapsed(started);
                }
                if (!opening && !state.backgroundEffectJob &&
                    result["milestones_ms"]["committed"].is_null())
                {
                    result["milestones_ms"]["committed"] = elapsed(started);
                }
                if (CUPUACU_BENCHMARK_SDL && !state.longTask.active &&
                    renderReady(state) &&
                    result["milestones_ms"]["view_ready"].is_null())
                {
                    result["milestones_ms"]["view_ready"] = elapsed(started);
                }
                auto &session = state.getActiveDocumentSession();
                if (sawAudio &&
                    !result["milestones_ms"]["committed"].is_null() &&
                    !session.getWaveformCacheBuildProgress() &&
                    session.getWaveformCache(0).levelsCount() > 0 &&
                    !session.getWaveformCache(0).hasDirtyBlocks() &&
                    session.getWaveformCache(1).levelsCount() > 0 &&
                    !session.getWaveformCache(1).hasDirtyBlocks() &&
                    result["milestones_ms"]["waveform_complete"].is_null())
                {
                    result["milestones_ms"]["waveform_complete"] =
                        elapsed(started);
                }
                require(error.empty(), error);
            } while (busy(state) ||
                     (CUPUACU_BENCHMARK_SDL && !renderReady(state)));
            persistence::flushScheduledClipboardSnapshots();
            result["milestones_ms"]["background_complete"] = elapsed(started);
            measurement.SetIterationTime(elapsed(started) / 1000.0);
        }
        captureMetrics();
        if (probes)
        {
            probes->finish();
            if (name == "responsive_stall")
            {
                require(result["event_latency"]["max_ms"].get<double>() >= 50,
                        "Stall probe failed");
            }
        }
        validateSamples(state.getActiveDocumentSession().document,
                        expectedFrames, expected);
        if (navigating || name == "waveform_build")
        {
            auto &session = state.getActiveDocumentSession();
            for (int64_t i = 0; i + 128 <= expectedFrames;
                 i += std::max<int64_t>(128, (expectedFrames / 4096) * 128))
            {
                gui::Peak peak{};
                require(gui::computeWaveformPeakForSampleWindow(
                            session, 0, 0, 128, 1, i, i + 128, peak),
                        "Missing waveform result");
                float low = expected(i, 0), high = low;
                for (int64_t j = i; j < i + 128; ++j)
                {
                    low = std::min(low, expected(j, 0));
                    high = std::max(high, expected(j, 0));
                }
                require(peak.min == low && peak.max == high,
                        "Waveform result mismatch");
            }
        }
        if (retained)
        {
            validateSamples(*retained, frames, sampleAt);
        }
        if (name == "copy" || name == "paste")
        {
            require(state.clipboard.getFrameCount() == 1000,
                    "Clipboard length mismatch");
            for (int ch = 0; ch < channels; ++ch)
            {
                for (int i = 0; i < 1000; ++i)
                {
                    require(state.clipboard.getSample(ch, i) ==
                                sampleAt(i + 1000, ch),
                            "Clipboard sample mismatch");
                }
            }
        }
#if CUPUACU_WORK_METRICS
        if (name == "gain_fixed" || name == "sample_shared")
        {
            require(result["work"]["full_buffer_clones"].get<uint64_t>() == 0,
                    "Small edit cloned the entire sample buffer");
            require(result["work"]["sample_bytes_copied"].get<uint64_t>() <=
                        512 * 1024,
                    "Small edit copied more than its bounded sample pages");
        }
        if (name == "waveform_build")
        {
            require(result["work"]["base_peaks_rebuilt"].get<uint64_t>() ==
                        uint64_t(channels *
                                 ((frames +
                                   gui::WaveformCache::BASE_BLOCK_SIZE - 1) /
                                  gui::WaveformCache::BASE_BLOCK_SIZE)),
                    "Peak observation mismatch");
        }
#endif
        result["validated"] = true;
    }

    uint64_t peakRss()
    {
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS counters{};
        require(GetProcessMemoryInfo(GetCurrentProcess(), &counters,
                                     sizeof(counters)),
                "RSS query failed");
        return counters.PeakWorkingSetSize;
#else
        rusage usage{};
        require(getrusage(RUSAGE_SELF, &usage) == 0, "RSS query failed");
#ifdef __APPLE__
        return uint64_t(usage.ru_maxrss);
#else
        return uint64_t(usage.ru_maxrss) * 1024;
#endif
#endif
    }
} // namespace

int runMain(int argc, char **argv)
{
    std::filesystem::path output;
    try
    {
        require(argc == 3,
                "Usage: cupuacu-benchmarks request.json result.json");
        output = argv[2];
        std::ifstream input(argv[1]);
        input >> request;
        if (request.value("generate", false))
        {
            generateFixture(request.at("fixture").get<std::string>(),
                            request.at("frames").get<int64_t>(),
                            request.value("format", "wav"));
            std::ofstream(output)
                << Json{{"generated", true}, {"fixture_version", 2}}.dump(2);
            return 0;
        }
        require(build::buildConfiguration() == "Release",
                "Benchmarks require a Release build");
        require(
            SDL_Init(CUPUACU_BENCHMARK_SDL ? SDL_INIT_VIDEO : SDL_INIT_EVENTS),
            SDL_GetError());
#if CUPUACU_BENCHMARK_SDL
        require(std::string(SDL_GetCurrentVideoDriver()) == "x11",
                "SDL benchmark requires x11/Xvfb");
        require(TTF_Init(), SDL_GetError());
#endif
        result = {
            {"schema_version", 1},
            {"request", request},
            {"validated", false},
            {"environment",
             {{"revision", benchmarkRevision},
              {"dirty", benchmarkDirty},
              {"source_fingerprint", benchmarkSourceFingerprint},
              {"build", build::diagnosticReport()},
              {"dependencies", benchmarkDependencies},
              {"compiler", build::compilerDescription()},
              {"configuration", build::buildConfiguration()},
              {"renderer", CUPUACU_BENCHMARK_SDL ? "software/x11" : "none"},
              {"system_ram_mib", SDL_GetSystemRAM()},
              {"cpu_count", SDL_GetNumLogicalCPUCores()},
              {"diagnostic", bool(CUPUACU_WORK_METRICS)},
              {"sdl", bool(CUPUACU_BENCHMARK_SDL)}}},
            {"milestones_ms",
             {{"command_return", nullptr},
              {"committed", nullptr},
              {"audio_available", nullptr},
              {"view_ready", nullptr},
              {"waveform_complete", nullptr},
              {"background_complete", nullptr}}}};
        char program[] = "cupuacu-benchmarks";
        char *args[] = {program, nullptr};
        int count = 1;
        benchmark::Initialize(&count, args);
        benchmark::RegisterBenchmark(
            request.at("scenario").get<std::string>().c_str(),
            [](benchmark::State &state)
            {
                try
                {
                    scenario(state);
                }
                catch (const std::exception &e)
                {
                    result["error"] = e.what();
                    state.SkipWithError(e.what());
                }
            })
            ->Iterations(1)
            ->Repetitions(1)
            ->UseManualTime();
        benchmark::RunSpecifiedBenchmarks();
        benchmark::Shutdown();
        result["peak_process_rss_bytes_including_setup"] = peakRss();
        std::ofstream(output) << result.dump(2) << '\n';
#if CUPUACU_BENCHMARK_SDL
        TTF_Quit();
#endif
        SDL_Quit();
        return result.value("validated", false) ? 0 : 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        if (!output.empty())
        {
            std::ofstream(output)
                << Json{{"error", e.what()}, {"validated", false}}.dump(2);
        }
        return 1;
    }
}

int main(int argc, char **argv)
{
#ifndef _WIN32
    if (argc == 2 && std::string(argv[1]) == "--server")
    {
        // This parent remains single-threaded and never opens a document or
        // initializes SDL. Children inherit only benchmark CPU metadata.
        (void)benchmark::CPUInfo::Get();
        std::cout << Json{{"ready", true}}.dump() << std::endl;
        std::string line;
        while (std::getline(std::cin, line))
        {
            const auto command = Json::parse(line);
            const auto pid = fork();
            if (pid == 0)
            {
                const std::string log = command.at("log");
                if (!std::freopen(log.c_str(), "w", stdout) ||
                    !std::freopen(log.c_str(), "a", stderr))
                {
                    _exit(2);
                }
                std::string input = command.at("request"),
                            output = command.at("result");
                char *childArgs[] = {argv[0], input.data(), output.data(),
                                     nullptr};
                const int code = runMain(3, childArgs);
                std::cout.flush();
                std::cerr.flush();
                _exit(code);
            }
            std::cout << Json{{"pid", pid}}.dump() << std::endl;
            if (pid < 0)
            {
                return 2;
            }
            int status = 0;
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
            {
            }
            std::cout << Json{{"exit_code", WIFEXITED(status)
                                                ? WEXITSTATUS(status)
                                                : 128 + WTERMSIG(status)}}
                             .dump()
                      << std::endl;
        }
        return 0;
    }
#endif
    return runMain(argc, argv);
}
