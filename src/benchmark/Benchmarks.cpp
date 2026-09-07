#include "actions/DocumentTabs.hpp"
#include "actions/markers/Split.hpp"
#include <latch>
#include "ApplicationLoop.hpp"
#include "BenchmarkBuild.hpp"
#include "BenchmarkSourceFingerprint.hpp"
#include "BuildInfo.hpp"
#include "actions/audio/EditCommands.hpp"
#include "actions/audio/RevisionRecording.hpp"
#include "persistence/RevisionPersistence.hpp"
#include "persistence/DocumentAutosave.hpp"
#include "actions/audio/SetSampleValue.hpp"
#include "effects/PeakAnalysis.hpp"
#include "actions/Zoom.hpp"
#include "file/LegacyAudioLoading.hpp"
#include "file/SndfilePath.hpp"
#include "file/OwnedAudioImport.hpp"
#include "file/DecodedImportCache.hpp"
#include "file/AudioFileWriter.hpp"
#include "playback/ReadAhead.hpp"
#include "storage/RecordingWriter.hpp"
#include "actions/effects/RevisionEffect.hpp"
#include "storage/AsyncAudioReader.hpp"
#include "storage/AudioEditRevision.hpp"
#include "waveform/WaveformViewport.hpp"
#include "file/m4a/M4aAlacWriter.hpp"
#include "file/m4a/M4aParser.hpp"
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
    uint64_t peakRss();
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

    void finishRevisionCommands(State *state)
    {
        const auto deadline = Clock::now() + std::chrono::seconds(10);
        while (!state->revisionCommands.empty())
        {
            actions::audio::processPendingRevisionCommands(state);
            require(Clock::now() < deadline, "Revision command timed out");
            if (!state->revisionCommands.empty())
            {
                std::this_thread::yield();
            }
        }
    }

    void editSample(State *state, uint32_t channel, int64_t frame,
                    float oldValue, float value)
    {
        if (auto revision = state->getActiveDocumentSession().getEditRevision())
        {
            actions::audio::prepareRevisionSampleEdit(
                state, std::move(revision), channel, frame, value);
            finishRevisionCommands(state);
        }
        else
        {
            state->addAndDoUndoable(
                std::make_shared<actions::audio::SetSampleValue>(
                    state, channel, frame, oldValue, value));
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
        if (format == "m4a")
        {
            class FixtureReader final : public storage::AudioReader
            {
                int64_t frames;
            public:
                explicit FixtureReader(int64_t count) : frames(count) {}
                storage::AudioShape shape() const override
                { return {frames, channels, sampleRate, SampleFormat::PCM_S16}; }
                void readChannel(int channel, int64_t first,
                                 std::span<float> output) const override
                {
                    validateRange(shape(), channel, first, output.size());
                    for (auto &value : output)
                        value = sampleAt(first++, channel) * (32768.0f / 32767.0f);
                }
            } reader(frames);
            file::m4a::writeAlacM4aFile(reader, {}, path, 16);
            return;
        }
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
                "Cupuacu benchmark", viewportWidth + 16, 500,
                SDL_WINDOW_HIDDEN);
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
        if (file::DecodedImportCache::hasPendingWork() || waveform::hasScheduledPersistentWaveformCacheWork())
        {
            return true;
        }
        if (state.backgroundEffectJob || state.backgroundOpenJob ||
            state.backgroundSaveJob || state.backgroundAutosaveJob ||
            !state.pendingOpenFiles.empty() || state.longTask.active)
        {
            return true;
        }
        for (auto &tab : state.tabs)
        {
            if (tab.session.pendingImportedPeaks)
            {
                return true;
            }
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
            if (scenario == "zoom_unaligned" && step % 3 == 2)
            {
                // Power-of-two fixtures and viewport widths otherwise align
                // every fit-to-file query to coarse peak boundaries.
                spp = (double(frames) - 17.0) / (viewportWidth - 3.0);
            }
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
                    ++stats.windowsRequested;
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

    #include "LargeFileWorkflow.hpp"
    #include "RestoredClipboardPaste.hpp"
    #include "AudioMemory.hpp"
#include "IndexPaging.hpp"
#include "PeakPaging.hpp"

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
        if (name == "legacy_metadata_paged" ||
            name == "legacy_metadata_resident")
        {
            legacyMetadataScenario(measurement, frames,
                                   name == "legacy_metadata_paged");
            return;
        }
        if (name == "working_memory")
        {
            workingMemoryScenario(measurement, frames);
            return;
        }
        if (name == "index_archive")
        {
            indexArchiveScenario(measurement, frames);
            return;
        }
        if (name == "edit_metadata")
        {
            editMetadataScenario(measurement, frames);
            return;
        }
        if (name == "index_paged" || name == "index_resident")
        {
            indexPagingScenario(measurement, frames, name == "index_paged");
            return;
        }
        if (name == "peak_paged" || name == "peak_resident" ||
            name == "peak_streaming" || name == "peak_progressive")
        {
            peakPagingScenario(measurement, frames, name == "peak_paged",
                               name == "peak_streaming",
                               name == "peak_progressive");
            return;
        }
        if (name == "audio_memory")
        {
            audioMemoryScenario(measurement, frames);
            return;
        }
        if (name == "recovery_legacy")
        {
            const auto root =
                std::filesystem::path(request.at("root").get<std::string>());
            const auto path = root / "legacy-snapshot";
            // Generate the real version-2 format with bounded scratch.
            // Deliberately leave old peaks dirty: recovery must summarize the
            // recovered samples.
            {
                std::ofstream out(path, std::ios::binary);
                const char magic[] = "CUPUACU_AUTOSAVE";
                out.write(magic, sizeof(magic));
                auto integer = [&](uint64_t v, int width = 8)
                {
                    for (int i = 0; i < width; ++i)
                    {
                        out.put(char(v >> (8 * i)));
                    }
                };
                integer(2, 4);
                integer(uint32_t(SampleFormat::FLOAT32), 4);
                integer(sampleRate, 4);
                integer(channels);
                integer(frames);
                integer(0, 4);
                integer(0);
                integer(channels);
                for (int c = 0; c < channels; ++c)
                {
                    integer(frames);
                    integer(0);
                    integer(0);
                    integer(0, 4);
                }
                std::array<float, 16384 * channels> buffer;
                for (int64_t first = 0; first < frames;)
                {
                    const auto count = std::min<int64_t>(16384, frames - first);
                    for (int64_t i = 0; i < count; ++i)
                    {
                        for (int c = 0; c < channels; ++c)
                        {
                            buffer[i * channels + c] = sampleAt(first + i, c);
                        }
                    }
                    out.write(reinterpret_cast<const char *>(buffer.data()),
                              count * channels * sizeof(float));
                    first += count;
                }
                require(out.good(), "Legacy fixture write failed");
            }
            State state;
            state.paths.reset();
            auto &session = state.getActiveDocumentSession();
            for (auto iteration : measurement)
            {
                (void)iteration;
                const auto began = Clock::now();
                require(
                    persistence::loadDocumentAutosaveSnapshot(path, session),
                    "Legacy recovery failed");
                const auto ms = elapsed(began);
                result["milestones_ms"]["background_complete"] = ms;
                measurement.SetIterationTime(ms / 1000.0);
            }
            // Also supports the pre-migration loader for matched reference
            // runs.
            result["legacy_recovery"]["revision_backed"] =
                session.hasReadRevision() ? 1 : 0;
            if (session.hasReadRevision())
            {
                uint64_t readBefore = 0, written = 0;
                std::shared_ptr<storage::AudioBlockStore> store;
                session.getEditRevision()->visitSourceRanges(
                    0, 0, frames,
                    [&](const auto &r)
                    {
                        store = r.source->blockStore();
                    });
                std::tie(readBefore, written) = store->ioBytes();
                const auto began = Clock::now();
                actions::audio::performRevisionCommand(
                    &state, actions::audio::RevisionCommand::Delete, frames / 2,
                    1);
                finishRevisionCommands(&state);
                const auto edit = elapsed(began);
                const auto undoAt = Clock::now();
                state.undo();
                const auto undo = elapsed(undoAt);
                require(store->ioBytes().first == readBefore,
                        "Recovered edit read sample data");
                require(store->ioBytes().second == written,
                        "Recovered edit wrote sample data");
                result["legacy_recovery"].update(
                    {{"sample_bytes_written", written},
                     {"sample_bytes_read", readBefore},
                     {"edit_ms", edit},
                     {"undo_ms", undo},
                     {"import_peak_rss_bytes", peakRss()}});
                require(written == uint64_t(frames) * channels * sizeof(float),
                        "Legacy conversion copied extra audio");
            }
            const auto reopenAt = Clock::now();
            require(persistence::loadDocumentAutosaveSnapshot(path, session),
                    "Durable recovery reopen failed");
            result["legacy_recovery"]["durable_reopen_ms"] = elapsed(reopenAt);
            std::array<float, 16384> buffer;
            for (int c = 0; c < channels; ++c)
            {
                for (int64_t first = 0; first < frames;)
                {
                    const auto count =
                        std::min<int64_t>(buffer.size(), frames - first);
                    session.getAudioReader()->readChannel(
                        c, first, std::span(buffer).first(count));
                    for (int64_t i = 0; i < count; ++i)
                    {
                        require(buffer[i] == sampleAt(first + i, c),
                                "Legacy recovered sample mismatch");
                    }
                    first += count;
                }
            }
            result["validated"] = true;
            return;
        }
        if (name == "m4a_metadata")
        {
            using namespace file::m4a;
            const auto path = std::filesystem::path(request.at("root").get<std::string>()) / "metadata.m4a";
            const auto cookie = file::alac::makeEncoderCookie({sampleRate, channels, 16, 4096});
            require(bool(cookie), "ALAC cookie unavailable");
            AlacMovieDescription description;
            description.sampleRate = sampleRate;
            description.frameCount = frames;
            description.framesPerPacket = 4096;
            description.sampleEntry = {channels, 16, sampleRate, cookie->bytes};
            const auto packets = (uint64_t(frames) + 4095) / 4096;
            description.packetSizes.assign(packets, 5000);
            const uint64_t payload = packets * 5000;
            const std::vector<DocumentMarker> markers{{1, frames - 17, "Boundary"}};
            {
                std::ofstream output(path, std::ios::binary);
                beginAlacM4a(output);
                output.seekp(ftypAtom().size() + 16 + payload);
                finishAlacM4a(output, std::move(description), payload, markers);
                require(output.good(), "Sparse M4A metadata write failed");
            }
            M4aParsedAlacFile parsed;
            for (auto iteration : measurement)
            {
                (void)iteration;
                const auto began = Clock::now();
                parsed = parseAlacM4aFile(path);
                const auto duration = elapsed(began);
                result["milestones_ms"]["background_complete"] = duration;
                measurement.SetIterationTime(duration / 1000.);
            }
            require(parsed.frameCount == uint64_t(frames) && parsed.markers == markers,
                    "M4A duration or chapters narrowed");
            require(parsed.packetSizes.size() == packets && parsed.packetOffsets.size() == packets,
                    "M4A packet count mismatch");
            uint64_t totalFrames = 0;
            for (std::size_t i = 0; i < packets; ++i)
            {
                require(parsed.packetOffsets[i] == ftypAtom().size() + 16 + i * 5000ull,
                        "M4A packet offset mismatch");
                totalFrames += parsed.packetFrameCounts[i];
            }
            require(totalFrames == uint64_t(frames), "M4A timing table mismatch");
            result["m4a_metadata"] = {{"frames", frames}, {"packets", packets},
                {"logical_file_bytes", std::filesystem::file_size(path)},
                {"table_bytes", std::filesystem::file_size(path) - payload}};
            result["validated"] = true;
            return;
        }
        if (name == "new_document_edit")
        {
            State state;
            state.paths.reset(); // Measure editing separately from autosave.
            actions::createNewDocument(&state, sampleRate, SampleFormat::FLOAT32,
                                       channels, false);
            auto &session = state.getActiveDocumentSession();
            if (!session.hasReadRevision())
                session.undoStore.attach(std::filesystem::path(
                    request.at("root").get<std::string>()) / "undo");
            for (auto iteration : measurement)
            {
                (void)iteration;
                performance::resetWork();
                const auto started = Clock::now();
                actions::audio::performInsertSilence(&state, frames);
                finishRevisionCommands(&state);
                result["new_document"]["insert_silence_ms"] = elapsed(started);
                auto began = Clock::now();
                editSample(&state, 0, 17, 0.f, .5f);
                result["new_document"]["point_edit_ms"] = elapsed(began);
                began = Clock::now();
                state.undo();
                result["new_document"]["undo_ms"] = elapsed(began);
                began = Clock::now();
                state.redo();
                result["new_document"]["redo_ms"] = elapsed(began);
                const auto duration = elapsed(started);
                result["milestones_ms"]["background_complete"] = duration;
                measurement.SetIterationTime(duration / 1000.);
            }
            captureMetrics();
            require(session.document.getFrameCount() == frames, "New document length mismatch");
            require(state.getActiveUndoables().size() == 2, "New document history mismatch");
            auto reader = session.getAudioReader();
            std::array<float, 16384> samples;
            for (int c = 0; c < channels; ++c)
                for (int64_t first = 0; first < frames; first += samples.size())
                {
                    auto out = std::span(samples).first(std::min<int64_t>(samples.size(), frames - first));
                    reader->readChannel(c, first, out);
                    for (std::size_t i = 0; i < out.size(); ++i)
                        require(out[i] == (c == 0 && first + i == 17 ? .5f : 0.f),
                                "New document sample mismatch");
                }
            result["validated"] = true;
            return;
        }
        if (name == "paste_restored_empty")
        {
            restoredClipboardPaste(measurement);
            return;
        }
        if (name == "large_file_workflow")
        {
            largeFileWorkflow(measurement);
            return;
        }
        if (name == "playback_memory" || name == "playback_owned")
        {
            DocumentSession session;
            std::shared_ptr<storage::DecodedBlockCache> cache;
            const bool owned = name == "playback_owned";
            if (owned)
            {
                cache = std::make_shared<storage::DecodedBlockCache>(2 * 1024 *
                                                                     1024);
                auto imported = file::importOwnedAudio(
                    request.at("fixture").get<std::string>(),
                    std::filesystem::path(
                        request.at("root").get<std::string>()) /
                        "playback-source",
                    cache);
                session.document = std::move(imported.metadata.document);
                session.bindReadRevision(
                    storage::AudioEditRevision::from(imported.audio));
            }
            else
            {
                auto loaded = file::legacy::loadAudioFile(
                    request.at("fixture").get<std::string>());
                session.document = std::move(loaded.document);
            }
            audio::AudioDevices device(false); // No device stream or GUI.
            std::array<float, 512> output;
            std::vector<double> callbacks, starts, firstData, stops;
            double callbackMs = 0;
            for (auto iteration : measurement)
            {
                (void)iteration;
                for (int step = 0; step < 8; ++step)
                {
                    const int64_t start = 17 + (frames - 8192) * step / 8;
                    const int64_t end = start + 4096;
                    audio::Play play{};
                    play.document = &session.document;
                    if (owned)
                    {
                        play.readerSnapshot = session.getAudioReader();
                    }
                    play.startPos = start;
                    play.endPos = end;
                    play.loopEnabled = true;
                    play.selectedChannels = SelectedChannels::BOTH;
                    auto began = Clock::now();
                    require(device.enqueue(std::move(play)),
                            "Playback start rejected");
                    starts.push_back(elapsed(began));
                    bool received = false;
                    int64_t warmFrames = 0, previous = start;
                    while (warmFrames < 8192)
                    {
                        device.processCallbackCycle(nullptr, output.data(),
                                                    256);
                        const auto pos = device.getPlaybackPosition();
                        require(!device.takePlaybackFailure(),
                                "Playback read failed");
                        if (pos != previous)
                        {
                            if (!received)
                            {
                                firstData.push_back(elapsed(began));
                                received = true;
                            }
                            warmFrames += pos >= previous
                                              ? pos - previous
                                              : end - previous + pos - start;
                            previous = pos;
                        }
                        else
                        {
                            std::this_thread::sleep_for(
                                std::chrono::milliseconds(1));
                        }
                        require(elapsed(began) < 10000,
                                "Playback warmup timed out");
                    }
                    const auto initialUnderruns =
                        device.getPlaybackUnderrunFrames();
                    int64_t expectedFrame = device.getPlaybackPosition();
                    for (int block = 0; block < 128; ++block)
                    {
                        const auto started = Clock::now();
                        device.processCallbackCycle(nullptr, output.data(),
                                                    256);
                        callbacks.push_back(elapsed(started));
                        callbackMs += callbacks.back();
                        // Validate outside the callback timer, including wraps.
                        for (int i = 0; i < 256; ++i)
                        {
                            if (expectedFrame == end)
                            {
                                expectedFrame = start;
                            }
                            for (int ch = 0; ch < 2; ++ch)
                            {
                                require(output[i * 2 + ch] ==
                                            sampleAt(expectedFrame, ch),
                                        "Playback sample mismatch");
                            }
                            ++expectedFrame;
                        }
                    }
                    require(device.getPlaybackUnderrunFrames() ==
                                initialUnderruns,
                            "Warm loop underrun");
                    began = Clock::now();
                    device.enqueue(audio::Stop{});
                    device.processCallbackCycle(nullptr, output.data(), 256);
                    stops.push_back(elapsed(began));
                    require(!device.isPlaying(), "Playback stop failed");
                    device.servicePlayback();
                }
                measurement.SetIterationTime(callbackMs / 1000.0);
            }
            // Pace a separate sequential pass at the fixture sample rate to
            // exercise replenishment. This is a simulated callback clock, not
            // a hardware-device or cold-disk deadline measurement.
            audio::Play sequential{};
            sequential.document = &session.document;
            if (owned)
            {
                sequential.readerSnapshot = session.getAudioReader();
            }
            const int64_t sequentialStart = frames / 3;
            sequential.startPos = sequentialStart;
            sequential.endPos = sequentialStart + 32768 + 256;
            sequential.selectedChannels = SelectedChannels::BOTH;
            require(device.enqueue(std::move(sequential)),
                    "Sequential playback rejected");
            auto waiting = Clock::now();
            do
            {
                device.processCallbackCycle(nullptr, output.data(), 256);
                require(!device.takePlaybackFailure(),
                        "Sequential read failed");
                require(elapsed(waiting) < 10000,
                        "Sequential startup timed out");
                if (device.getPlaybackPosition() == sequentialStart)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            } while (device.getPlaybackPosition() == sequentialStart);
            std::vector<double> sequentialCallbacks;
            const auto pacedStart = Clock::now();
            for (int block = 0; block < 128; ++block)
            {
                std::this_thread::sleep_until(
                    pacedStart +
                    std::chrono::nanoseconds(int64_t(block + 1) * 256 *
                                             1000000000 / sampleRate));
                const auto before = device.getPlaybackPosition();
                const auto began = Clock::now();
                device.processCallbackCycle(nullptr, output.data(), 256);
                sequentialCallbacks.push_back(elapsed(began));
                const auto after = device.getPlaybackPosition();
                require(after >= before && after - before <= 256,
                        "Invalid sequential position");
                for (int i = 0; i < 256; ++i)
                {
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        require(
                            output[i * 2 + ch] ==
                                (i < after - before ? sampleAt(before + i, ch)
                                                    : 0),
                            "Sequential sample or underrun silence mismatch");
                    }
                }
            }
            require(!device.takePlaybackFailure(), "Sequential read failed");
            result["playback"]["sequential_underrun_frames"] =
                device.getPlaybackUnderrunFrames();
            std::sort(sequentialCallbacks.begin(), sequentialCallbacks.end());
            const auto sequentialP99 =
                sequentialCallbacks[sequentialCallbacks.size() * 99 / 100];
            device.enqueue(audio::Stop{});
            device.processCallbackCycle(nullptr, output.data(), 256);
            device.servicePlayback();
            for (auto *values : {&callbacks, &starts, &firstData, &stops})
            {
                std::sort(values->begin(), values->end());
            }
            result["playback"].update(
                {{"sequential_callback_p99_ms", sequentialP99},
                 {"callback_p50_ms", callbacks[callbacks.size() / 2]},
                 {"callback_p99_ms", callbacks[callbacks.size() * 99 / 100]},
                 {"callback_max_ms", callbacks.back()},
                 {"start_p50_ms", starts[starts.size() / 2]},
                 {"first_data_p50_ms", firstData[firstData.size() / 2]},
                 {"first_data_max_ms", firstData.back()},
                 {"stop_max_ms", stops.back()},
                 {"read_ahead_sample_bytes",
                  owned ? playback::ReadAhead::sampleBytes : 0}});
            if (cache)
            {
                require(cache->stats().peakResidentBytes <= 2 * 1024 * 1024,
                        "Playback cache exceeded budget");
                result["bounded_storage"]["peak_cached_sample_bytes"] =
                    cache->stats().peakResidentBytes;
            }
            result["milestones_ms"]["background_complete"] = callbackMs;
            result["validated"] = true;
            return;
        }
        if (name.starts_with("checkpoint_") || name == "recovery_owned")
        {
            State state;
            state.paths.reset();
            const auto root =
                std::filesystem::path(request.at("root").get<std::string>());
            auto imported = file::importOwnedAudio(
                request.at("fixture").get<std::string>(), root / "working",
                std::make_shared<storage::DecodedBlockCache>(1024 * 1024));
            auto &session = state.getActiveDocumentSession();
            session.document = std::move(imported.metadata.document);
            session.setCurrentFile(request.at("fixture").get<std::string>(),
                                   imported.metadata.exportSettings);
            session.bindReadRevision(
                storage::AudioEditRevision::from(imported.audio));
            const auto path = root / "checkpoint";
            const bool initial = name == "checkpoint_initial_owned";
            const bool recovery = name == "recovery_owned";
            const int historyCount =
                name == "checkpoint_history_owned" ? 1000 : 0;
            for (int i = 0; i < historyCount; ++i)
            {
                editSample(&state, 0, 100 + i, sampleAt(100 + i, 0), -.125f);
            }
            if (!initial)
            {
                persistence::RevisionPersistence::save(
                    path, *persistence::RevisionPersistence::capture(
                              session, state.getActiveTab()));
            }
            if (!initial && !recovery)
            {
                editSample(&state, 1, 17, sampleAt(17, 1), -.25f);
            }
            auto archive = storage::RevisionArchive::open(path);
            const auto before = archive->stats;
            const auto sourceReads =
                imported.audio->blockStore()->ioBytes().first;
            State restored;
            restored.paths.reset();
            double captureMs = 0;
            for (auto iteration : measurement)
            {
                (void)iteration;
                const auto started = Clock::now();
                if (recovery)
                {
                    persistence::RevisionPersistence::load(
                        path, restored.getActiveDocumentSession());
                    require(persistence::RevisionPersistence::installHistory(
                                &restored, 0),
                            "Recovery history install failed");
                }
                else
                {
                    auto cp = persistence::RevisionPersistence::capture(
                        session, state.getActiveTab());
                    captureMs = elapsed(started);
                    persistence::RevisionPersistence::save(path, *cp);
                }
                const auto complete = elapsed(started);
                result["revision_persistence"]["capture_ms"] = captureMs;
                result["revision_persistence"]["completion_ms"] = complete;
                measurement.SetIterationTime(complete / 1000.0);
            }
            result["revision_persistence"]["sample_bytes_copied"] =
                archive->stats.sampleBytes - before.sampleBytes;
            result["revision_persistence"]["original_bytes_copied"] =
                archive->stats.sourceBytes - before.sourceBytes;
            result["revision_persistence"]["metadata_bytes_written"] =
                archive->stats.metadataBytes - before.metadataBytes;
            result["revision_persistence"]["nodes_written"] =
                archive->stats.nodes - before.nodes;
            result["revision_persistence"]["history_entries"] =
                historyCount + (initial || recovery ? 0 : 1);
            require(imported.audio->blockStore()->ioBytes().first ==
                        sourceReads,
                    "Checkpoint decoded original audio");
            if (!initial)
            {
                require(archive->stats.sampleBytes == before.sampleBytes,
                        "Incremental checkpoint copied audio");
                require(archive->stats.sourceBytes == before.sourceBytes,
                        "Incremental checkpoint copied original bytes");
            }
            if (!recovery)
            {
                persistence::RevisionPersistence::load(
                    path, restored.getActiveDocumentSession());
                require(persistence::RevisionPersistence::installHistory(
                            &restored, 0),
                        "Checkpoint recovery failed");
            }
            const auto &recovered = restored.getActiveDocumentSession();
            auto audio = recovered.getEditRevision();
            require(audio && audio->shape().frames == frames,
                    "Recovered duration mismatch");
            audio->visitSourceRanges(
                0, 0, frames,
                [&](const auto &range)
                {
                    if (range.source)
                    {
                        require(range.source->blockStore()->ioBytes().first ==
                                    0,
                                "Recovery read samples before use");
                    }
                });
            std::array<float, 16384> buffer;
            for (int c = 0; c < channels; ++c)
            {
                for (int64_t f = 0; f < frames; f += buffer.size())
                {
                    const auto count =
                        std::min<int64_t>(buffer.size(), frames - f);
                    audio->readChannel(c, f, std::span(buffer).first(count));
                    for (int64_t i = 0; i < count; ++i)
                    {
                        const auto at = f + i;
                        const auto expected =
                            c == 0 && at >= 100 && at < 100 + historyCount
                                ? -.125f
                            : c == 1 && at == 17 && !initial && !recovery
                                ? -.25f
                                : sampleAt(at, c);
                        require(buffer[i] == expected,
                                "Recovered sample mismatch");
                    }
                }
            }
            require(
                restored.getActiveTab()->undoables.size() ==
                    std::size_t(historyCount + (!initial && !recovery ? 1 : 0)),
                "Recovered history length mismatch");
            if (!initial && !recovery)
            {
                restored.undo();
                float value = 0;
                recovered.getEditRevision()->readChannel(1, 17, {&value, 1});
                require(value == sampleAt(17, 1), "Recovered undo mismatch");
                restored.redo();
                require(recovered.getEditRevision() == audio,
                        "Recovered redo lost root identity");
            }
            captureMetrics();
            result["validated"] = true;
            return;
        }
        if (name.starts_with("record_"))
        {
            State state;
            state.paths.reset();
            const auto root = std::filesystem::path(request.at("root").get<std::string>());
            const bool fresh = name == "record_new";
            auto &session = state.getActiveDocumentSession();
            std::shared_ptr<storage::AudioBlockStore> sourceStore;
            if (fresh)
                actions::createNewDocument(&state, sampleRate, SampleFormat::FLOAT32,
                                           channels, false);
            else
            {
                auto imported = file::importOwnedAudio(
                    request.at("fixture").get<std::string>(), root / "audio",
                    std::make_shared<storage::DecodedBlockCache>(1024 * 1024));
                sourceStore = imported.audio->blockStore();
                session.document = std::move(imported.metadata.document);
                session.bindReadRevision(storage::AudioEditRevision::from(imported.audio));
            }
            const auto original = session.getEditRevision();
            const int64_t recordedFrames = name == "record_fixed_owned" ? 65536 : frames;
            const int64_t first = fresh ? 0 : 17;
            const auto reads = sourceStore ? sourceStore->ioBytes().first : 0;
            for (auto iteration : measurement)
            {
                (void)iteration;
                const auto started = Clock::now();
                actions::startRevisionRecording(&state, first, root / "recorded");
                auto recording = state.revisionRecording;
                result["recording"]["start_ms"] = elapsed(started);
                double maxHandoff = 0, maxPublish = 0;
                uint64_t publications = 0;
                for (int64_t f = 0; f < recordedFrames;)
                {
                    // Model bounded UI handoffs while allowing the disk worker
                    // to drain. No simulated realtime delay inflates throughput.
                    while (recording->writer.queuedChunks() > 256)
                    {
                        require(elapsed(started) < 60000, "Recording drain timed out");
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                    }
                    const auto handoff = Clock::now();
                    for (int i = 0; i < 64 && f < recordedFrames; ++i)
                    {
                        audio::RecordedChunk chunk{first + f,
                            uint32_t(std::min<int64_t>(256, recordedFrames - f)), channels, {}};
                        for (uint32_t j = 0; j < chunk.frameCount; ++j)
                            for (int c = 0; c < channels; ++c)
                                chunk.interleavedSamples[j * channels + c] = -sampleAt(first + f + j, c);
                        require(recording->writer.submit(chunk), "Recording queue rejected data");
                        f += chunk.frameCount;
                    }
                    maxHandoff = std::max(maxHandoff, elapsed(handoff));
                    const auto publish = Clock::now();
                    publications += actions::pollRevisionRecording(&state);
                    maxPublish = std::max(maxPublish, elapsed(publish));
                }
                recording->finishing = true;
                recording->writer.finish();
                while (state.revisionRecording)
                {
                    const auto publish = Clock::now();
                    publications += actions::pollRevisionRecording(&state);
                    maxPublish = std::max(maxPublish, elapsed(publish));
                    require(elapsed(started) < 60000, "Recording completion timed out");
                    if (state.revisionRecording)
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
                const auto complete = elapsed(started);
                const auto snapshot = recording->writer.snapshot();
                require(snapshot.error.empty(), snapshot.error);
                result["recording"]["completion_ms"] = complete;
                result["recording"]["max_handoff_ms"] = maxHandoff;
                result["recording"]["max_publication_ms"] = maxPublish;
                result["recording"]["publications"] = publications;
                result["recording"]["sample_bytes_written"] = snapshot.sampleBytesWritten;
                result["recording"]["peak_queue_chunks"] = recording->writer.peakQueuedChunks();
                measurement.SetIterationTime(complete / 1000.0);
            }
            const auto sourceReads = sourceStore ? sourceStore->ioBytes().first - reads : 0;
            result["recording"]["original_sample_bytes_read"] = sourceReads;
            require(sourceReads == 0, "Recording read overwritten or untouched original samples");
            require(state.getActiveTab()->undoables.size() == 1, "Recording history is not one root switch");
            const auto recorded = session.getEditRevision();
            require(recorded->shape().frames == std::max(original->shape().frames, first + recordedFrames), "Recording duration mismatch");
            std::array<float, 16384> samples;
            for (int c = 0; c < channels; ++c)
                for (int64_t f = 0; f < recorded->shape().frames; f += samples.size())
                {
                    const auto count = std::min<int64_t>(samples.size(), recorded->shape().frames - f);
                    recorded->readChannel(c, f, std::span(samples).first(count));
                    for (int64_t i = 0; i < count; ++i)
                        require(samples[i] == ((f+i >= first && f+i < first + recordedFrames) ? -sampleAt(f+i,c) : sampleAt(f+i,c)),
                                "Recorded sample mismatch");
                }
            auto undo = state.getActiveTab()->undoables.back();
            const auto undoStarted = Clock::now();
            undo->undo();
            result["recording"]["undo_ms"] = elapsed(undoStarted);
            require(session.getEditRevision() == original, "Recording undo lost original root");
            undo->redo();
            require(session.getEditRevision() == recorded, "Recording redo lost recorded root");
            captureMetrics();
            result["validated"] = true;
            return;
        }
        if (name.starts_with("save_worker_"))
        {
            const bool owned = name.ends_with("owned");
            const bool preserving =
                name.find("preserving") != std::string::npos;
            State state;
            state.paths
                .reset(); // Worker-only: no live application paths/autosave.
            auto &session = state.getActiveDocumentSession();
            const auto root =
                std::filesystem::path(request.at("root").get<std::string>());
            const auto fixture =
                std::filesystem::path(request.at("fixture").get<std::string>());
            const auto output = root / "saved.wav";
            std::filesystem::path reference = fixture;
            std::shared_ptr<storage::AudioBlockStore> store;
            if (owned)
            {
                auto imported = file::importOwnedAudio(
                    fixture, root / "audio",
                    std::make_shared<storage::DecodedBlockCache>(2 * 1024 *
                                                                 1024));
                session.document = std::move(imported.metadata.document);
                session.bindReadRevision(
                    storage::AudioEditRevision::from(imported.audio));
                reference = imported.audio->sourcePath();
                store = imported.audio->blockStore();
                auto before = session.getEditRevision();
                storage::AudioEditTransaction edit(*before);
                edit.replaceChannel(0, 10001, 1, nullptr, 0, 0, .25f);
                require(session.commitEditRevision(before, edit.finish(), {}),
                        "Save edit failed");
            }
            else
            {
                auto loaded = file::legacy::loadAudioFile(fixture);
                session.document = std::move(loaded.document);
                session.document.setSample(0, 10001, .25f);
            }
            const auto settings = *file::defaultExportSettingsForPath(
                output, session.document.getSampleFormat());
            const auto beforeIO = store ? store->ioBytes().first : 0;
            for (auto iteration : measurement)
            {
                (void)iteration;
                const auto started = Clock::now();
                actions::io::BackgroundSaveJob job(
                    1,
                    {preserving
                         ? actions::io::BackgroundSaveKind::SaveAsPreserving
                         : actions::io::BackgroundSaveKind::SaveAs,
                     output, reference, settings},
                    &state, session.document, {}, session.getEditRevision());
                job.start();
                result["save_worker"]["submission_ms"] = elapsed(started);
                for (;;)
                {
                    const auto snapshot = job.snapshot();
                    if (snapshot.completed)
                    {
                        require(snapshot.success, snapshot.error);
                        break;
                    }
                    require(elapsed(started) < 60000, "Save worker timed out");
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
                result["save_worker"]["completion_ms"] = elapsed(started);
                measurement.SetIterationTime(elapsed(started) / 1000.0);
            }
            if (store)
            {
                const auto reads = store->ioBytes().first - beforeIO;
                result["save_worker"]["decoded_sample_bytes_read"] = reads;
                if (preserving)
                {
                    require(reads == 0,
                            "Preserving save decoded unchanged audio");
                }
            }
            captureMetrics();
            int64_t checked = 0;
            auto savedOutput = file::decodeAudioFile(
                output,
                [&](const Document &, int64_t start, const float *samples,
                    int64_t count)
                {
                    for (int64_t i = 0; i < count; ++i)
                    {
                        for (int c = 0; c < channels; ++c)
                        {
                            const auto expected = start+i == 10001 && c == 0 ? .25f : sampleAt(start+i,c);
                            // Ordinary PCM export quantizes through libsndfile;
                            // preserving output must retain the source exactly.
                            require(std::abs(samples[i*channels+c]-expected) <= (preserving ? 0.f : 1.f/32768),
                                    "Saved sample mismatch");
                        }
                    }
                    checked += count;
                });
            require(checked == frames, "Saved length mismatch");
            require(savedOutput.exportSettings && savedOutput.exportSettings->subtype == settings.subtype,
                    "Saved PCM encoding mismatch");
            result["validated"] = true;
            return;
        }
        if (name == "export_memory_alac" || name == "export_memory_wav" ||
            name == "export_owned_alac" || name == "export_owned_wav")
        {
            DocumentSession session;
            std::shared_ptr<storage::DecodedBlockCache> cache;
            if (name.starts_with("export_owned"))
            {
                cache = std::make_shared<storage::DecodedBlockCache>(2 * 1024 *
                                                                     1024);
                auto imported = file::importOwnedAudio(
                    request.at("fixture").get<std::string>(),
                    std::filesystem::path(
                        request.at("root").get<std::string>()) /
                        "export-source",
                    cache);
                session.document = std::move(imported.metadata.document);
                session.bindReadRevision(
                    storage::AudioEditRevision::from(imported.audio));
            }
            else
            {
                auto loaded = file::legacy::loadAudioFile(
                    request.at("fixture").get<std::string>());
                session.document = std::move(loaded.document);
            }
            const auto reader = session.getAudioReader();
            const auto output =
                std::filesystem::path(request.at("root").get<std::string>()) /
                (name.ends_with("alac") ? "export.m4a" : "export.wav");
            const auto settings = file::defaultExportSettingsForPath(
                output, SampleFormat::PCM_S16);
            require(bool(settings), "Export settings unavailable");
            double totalMs = 0, firstProgress = 0;
            for (auto iteration : measurement)
            {
                (void)iteration;
                auto started = Clock::now();
                file::AudioFileWriter::writeFile(
                    *reader, {}, output, *settings,
                    [&](const std::string &, std::optional<double> progress)
                    {
                        if (progress && *progress > 0 && firstProgress == 0)
                        {
                            firstProgress = elapsed(started);
                        }
                    });
                totalMs = elapsed(started);
                measurement.SetIterationTime(totalMs / 1000.0);
            }
            int64_t checked = 0;
            file::decodeAudioFile(
                output,
                [&](const Document &, int64_t start, const float *samples,
                    int64_t count)
                {
                    for (int64_t i = 0; i < count; ++i)
                    {
                        for (int ch = 0; ch < channels; ++ch)
                        {
                            require(std::abs(samples[i * channels + ch] -
                                             sampleAt(start + i, ch)) <=
                                        1.0f / 32768,
                                    "Export sample mismatch");
                        }
                    }
                    checked += count;
                });
            require(checked == frames, "Export length mismatch");
            if (cache)
            {
                require(cache->stats().peakResidentBytes <= 2 * 1024 * 1024,
                        "Export decoded cache exceeded budget");
                result["bounded_storage"]["peak_cached_sample_bytes"] =
                    cache->stats().peakResidentBytes;
            }
            result["export"] = {
                {"first_progress_ms", firstProgress},
                {"output_bytes", std::filesystem::file_size(output)}};
            result["milestones_ms"]["background_complete"] = totalMs;
            result["validated"] = true;
            return;
        }
        if (name == "open_owned_session" || name == "open_memory_session")
        {
            DocumentSession session;
            std::shared_ptr<storage::AudioBlockStore> store;
            std::shared_ptr<storage::DecodedBlockCache> cache;
            if (name == "open_owned_session")
            {
                cache = std::make_shared<storage::DecodedBlockCache>(2 * 1024 *
                                                                     1024);
                auto imported = file::importOwnedAudio(
                    request.at("fixture").get<std::string>(),
                    std::filesystem::path(
                        request.at("root").get<std::string>()) /
                        "owned-audio",
                    cache);
                store = imported.audio->blockStore();
                session.document = std::move(imported.metadata.document);
                session.bindReadRevision(
                    storage::AudioEditRevision::from(imported.audio));
            }
            else
            {
                auto loaded = file::legacy::loadAudioFile(
                    request.at("fixture").get<std::string>());
                session.document = std::move(loaded.document);
                session.rebuildWaveformCacheSynchronously();
            }
            auto started = Clock::now();
            auto source = session.getViewportSource();
            require(bool(source), "Session viewport source unavailable");
            result["bounded_storage"]["source_snapshot_ms"] = elapsed(started);
            waveform::WaveformViewport worker(*source);
            std::vector<double> dispatch, completion;
            double totalMs = 0;
            for (auto iteration : measurement)
            {
                (void)iteration;
                for (int step = 0; step < 32; ++step)
                {
                    const double spp = std::array<double, 4>{
                        0.25, 7.3, 127.9, double(frames) / 1024}[step % 4];
                    const int64_t offset =
                        step % 4 == 3
                            ? 0
                            : std::max<int64_t>(0, frames - 1024 * spp - 4) *
                                  ((step * 37) % 32) / 31;
                    const waveform::ViewportRequest view{step % channels,
                                                         offset, spp, 1024};
                    started = Clock::now();
                    require(session.getViewportSource() == source,
                            "Unchanged session rebuilt its snapshot");
                    const auto generation = worker.submit(view);
                    dispatch.push_back(elapsed(started));
                    std::optional<waveform::WaveformViewport::Result> output;
                    while (!(output = worker.takePublished()))
                    {
                        require(elapsed(started) < 10000,
                                "Viewport completion timed out");
                        std::this_thread::yield();
                    }
                    completion.push_back(elapsed(started));
                    totalMs += completion.back();
                    require(output->generation == generation,
                            "Stale viewport published");
                    if (output->error)
                    {
                        std::rethrow_exception(output->error);
                    }
                    require(output->value && !output->value->pending,
                            "Viewport remained pending");
                    const auto &data = *output->value;
                    if (spp < 1)
                    {
                        for (std::size_t i = 0; i < data.samples.size(); ++i)
                        {
                            require(
                                data.samples[i] ==
                                    sampleAt(data.rawStart + i, view.channel),
                                "Smooth viewport sample mismatch");
                        }
                    }
                    else
                    {
                        for (int x = 0; x < view.width; ++x)
                        {
                            const auto first = std::min<int64_t>(
                                frames, std::floor(offset + x * spp));
                            const auto end = std::min<int64_t>(
                                frames, std::floor(offset + (x + 1) * spp));
                            const auto peak = data.peaks[x];
                            if (spp < 128)
                            {
                                auto expected = waveform::emptyPeak();
                                for (auto frame = first; frame < end; ++frame)
                                {
                                    const auto value =
                                        sampleAt(frame, view.channel);
                                    expected = waveform::combine(
                                        expected, {value, value});
                                }
                                require(expected.min == peak.min &&
                                            expected.max == peak.max,
                                        "Raw viewport peak mismatch");
                            }
                            else if (end > first)
                            {
                                for (const auto frame :
                                     {first, first + (end - first) / 2,
                                      end - 1})
                                {
                                    const auto value =
                                        sampleAt(frame, view.channel);
                                    require(
                                        peak.min <= value && peak.max >= value,
                                        "Overview excluded a visible sample");
                                }
                            }
                        }
                    }
                }
                measurement.SetIterationTime(totalMs / 1000.0);
            }
            started = Clock::now();
            worker.close();
            result["bounded_storage"]["close_ms"] = elapsed(started);
            worker.waitUntilClosed();
            std::sort(dispatch.begin(), dispatch.end());
            std::sort(completion.begin(), completion.end());
            result["bounded_storage"].update(
                {{"submit_p50_ms", dispatch[dispatch.size() / 2]},
                 {"submit_max_ms", dispatch.back()},
                 {"completion_p50_ms", completion[completion.size() / 2]},
                 {"completion_max_ms", completion.back()}});
            if (cache)
            {
                require(cache->stats().peakResidentBytes <= 2 * 1024 * 1024,
                        "Viewport cache exceeded budget");
                result["bounded_storage"]["peak_cached_sample_bytes"] =
                    cache->stats().peakResidentBytes;
                result["bounded_storage"]["sample_bytes_read"] =
                    store->ioBytes().first;
            }
            result["milestones_ms"]["background_complete"] = totalMs;
            result["validated"] = true;
            return;
        }
        if (name == "bulk_busy_edit_owned")
        {
            State state;
            const auto root =
                std::filesystem::path(request.at("root").get<std::string>());
            state.paths = std::make_unique<BenchPaths>(root);
            auto imported = file::importOwnedAudio(
                request.at("fixture").get<std::string>(), root / "audio",
                std::make_shared<storage::DecodedBlockCache>(2 * 1024 * 1024));
            auto revision = storage::AudioEditRevision::from(imported.audio);
            for (int i = 0; i < 2; ++i)
            {
                if (i)
                {
                    state.tabs.emplace_back();
                }
                auto &session = state.tabs[i].session;
                session.document = imported.metadata.document;
                session.bindReadRevision(revision);
            }
            std::promise<void> release;
            auto gate = release.get_future().share();
            struct Release
            {
                std::promise<void> &p;
                ~Release()
                {
                    p.set_value();
                }
            };
            std::latch started(2);
            auto block = [&]
            {
                started.count_down();
                gate.wait();
            };
            auto first = state.taskScheduler->submit(block, {});
            auto second = state.taskScheduler->submit(block, {});
            started.wait();
            {
                Release onExit{release};
                for (auto iteration : measurement)
                {
                    (void)iteration;
                    const auto begin = Clock::now();
                    selection(state, 1000, 1000);
                    require(actions::effects::queueAmplifyFade(
                                &state, {50, 50, 0, true}),
                            "Busy effect rejected");
                    result["coordination"]["submission_ms"] = elapsed(begin);
                    require(actions::switchToTab(&state, 1),
                            "Busy work prevented tab switch");
                    state.paths.reset();
                    const auto edit = Clock::now();
                    actions::audio::prepareRevisionEdit(
                        &state, "Set samples",
                        [](const actions::audio::RevisionEditState &before)
                        {
                            auto after = before;
                            storage::AudioEditTransaction transaction(
                                *before.audio);
                            transaction.replaceChannel(0, 0, 128, nullptr, 0, 0,
                                                       .25f);
                            after.audio = transaction.finish();
                            return after;
                        });
                    result["coordination"]["edit_submission_ms"] =
                        elapsed(edit);
                    require(state.revisionCommands.size() == 1 &&
                                state.tabs[1].undoables.empty(),
                            "Queued edit performed work before admission");
                    result["coordination"]["bulk_running"] =
                        state.taskScheduler->stats().running;
                    require(state.taskScheduler->stats().running == 2,
                            "Bulk jobs did not stay occupied");
                    measurement.SetIterationTime(elapsed(begin) / 1000.0);
                }
            }
            require(state.backgroundEffectJob->waitForCompletion(
                        std::chrono::seconds(60)),
                    "Busy effect timed out");
            actions::effects::processPendingEffectWork(&state);
            finishRevisionCommands(&state);
            require(state.tabs[0].undoables.size() == 1 &&
                        state.tabs[1].undoables.size() == 1,
                    "Cross-tab edits lost");
            std::array<float, 128> values;
            state.tabs[1].session.getAudioReader()->readChannel(0, 0, values);
            for (auto value : values)
            {
                require(value == .25f, "Busy edit mismatch");
            }
            captureMetrics();
            result["validated"] = true;
            return;
        }
        if (name.starts_with("effect_fixed_") ||
            name.starts_with("effect_all_"))
        {
            const bool owned = name.ends_with("owned");
            const bool whole = name.starts_with("effect_all_");
            State state;
            state.paths.reset(); // Operation-only cases exclude autosave and
                                 // live application state.
            auto &session = state.getActiveDocumentSession();
            const auto root =
                std::filesystem::path(request.at("root").get<std::string>());
            if (owned)
            {
                // Keep generated effect stores inside the runner-owned
                // temporary directory while preparing the worker.
                state.paths = std::make_unique<BenchPaths>(root);
                auto imported = file::importOwnedAudio(
                    request.at("fixture").get<std::string>(), root / "audio",
                    std::make_shared<storage::DecodedBlockCache>(2 * 1024 *
                                                                 1024));
                session.document = std::move(imported.metadata.document);
                session.bindReadRevision(
                    storage::AudioEditRevision::from(imported.audio));
            }
            else
            {
                initialize(session, frames);
                session.undoStore.attach(root / "undo");
            }
            const int64_t start = whole ? 0 : 1000;
            const int64_t count = whole ? frames : 1000;
            selection(state, start, count);
            std::string error;
            state.errorReporter =
                [&](const std::string &, const std::string &detail)
            {
                error = detail;
            };
            for (auto iteration : measurement)
            {
                (void)iteration;
                const auto started = Clock::now();
                require(actions::effects::queueAmplifyFade(&state,
                                                           {50, 50, 0, true}),
                        "Effect did not queue");
                result["effect_command"]["submission_ms"] = elapsed(started);
                state.paths.reset(); // Publication excludes autosave.
                require(state.backgroundEffectJob->waitForCompletion(
                            std::chrono::seconds(60)),
                        "Effect timed out");
                const auto publication = Clock::now();
                actions::effects::processPendingEffectWork(&state);
                result["effect_command"]["publication_ms"] =
                    elapsed(publication);
                result["effect_command"]["completion_ms"] = elapsed(started);
                measurement.SetIterationTime(elapsed(started) / 1000.0);
                require(error.empty(), error);
                require(state.getActiveUndoables().size() == 1,
                        "Effect did not commit");
            }
            captureMetrics();
            auto reader = session.getAudioReader();
            std::array<float, 16384> buffer;
            for (int c = 0; c < channels; ++c)
            {
                for (int64_t first = 0; first < frames; first += buffer.size())
                {
                    auto out = std::span(buffer).first(
                        std::min<int64_t>(buffer.size(), frames - first));
                    reader->readChannel(c, first, out);
                    for (std::size_t i = 0; i < out.size(); ++i)
                    {
                        const auto frame = first + int64_t(i);
                        const auto gain =
                            frame >= start && frame - start < count ? .5f : 1.f;
                        require(out[i] == sampleAt(frame, c) * gain,
                                "Streamed effect sample mismatch");
                    }
                }
            }
            result["validated"] = true;
            return;
        }
        if (name == "normalize_owned" || name == "normalize_memory" ||
            name == "normalize_legacy")
        {
            State state;
            state.paths.reset(); // Operation-only cases exclude autosave and
                                 // live application state.
            auto &session = state.getActiveDocumentSession();
            if (name == "normalize_owned")
            {
                auto imported = file::importOwnedAudio(
                    request.at("fixture").get<std::string>(),
                    std::filesystem::path(
                        request.at("root").get<std::string>()) /
                        "audio",
                    std::make_shared<storage::DecodedBlockCache>(2 * 1024 *
                                                                 1024));
                session.document = std::move(imported.metadata.document);
                session.bindReadRevision(
                    storage::AudioEditRevision::from(imported.audio));
            }
            else
            {
                initialize(session, frames);
            }
            selection(state, 17, frames - 34);
            float peak = 0;
            for (auto iteration : measurement)
            {
                (void)iteration;
                const auto started = Clock::now();
                if (name == "normalize_legacy")
                {
                    peak = effects::computeTargetPeakAbsolute(&state);
                }
                else
                {
                    effects::PeakAnalysis analysis(session.getAudioReader(),
                                                   session.getEditRevision(),
                                                   session.getViewportSource());
                    analysis.submit({17, frames - 34, {0, 1}});
                    result["peak_analysis"]["submission_ms"] = elapsed(started);
                    for (;;)
                    {
                        if (auto ready = analysis.takePublished())
                        {
                            if (ready->error)
                            {
                                std::rethrow_exception(ready->error);
                            }
                            require(ready->value.has_value(),
                                    "Peak result missing");
                            peak = *ready->value;
                            break;
                        }
                        require(elapsed(started) < 10000,
                                "Peak analysis timed out");
                        std::this_thread::yield();
                    }
                }
                result["peak_analysis"]["completion_ms"] = elapsed(started);
                measurement.SetIterationTime(elapsed(started) / 1000.0);
            }
            require(peak == .875f, "Normalization peak was not exact");
            captureMetrics();
            result["validated"] = true;
            return;
        }
        if (name == "edit_command_owned" || name == "edit_command_memory" ||
            name == "sample_command_owned" || name == "sample_command_memory")
        {
            const bool owned = name.ends_with("owned");
            const bool point = name.starts_with("sample_");
            State state;
            state.paths.reset(); // Operation-only cases exclude autosave and
                                 // live application state.
            auto &session = state.getActiveDocumentSession();
            auto cache =
                std::make_shared<storage::DecodedBlockCache>(2 * 1024 * 1024);
            std::shared_ptr<storage::AudioBlockStore> store;
            if (owned)
            {
                auto imported = file::importOwnedAudio(
                    request.at("fixture").get<std::string>(),
                    std::filesystem::path(
                        request.at("root").get<std::string>()) /
                        "owned-audio",
                    cache);
                session.document = std::move(imported.metadata.document);
                auto original =
                    storage::AudioEditRevision::from(imported.audio);
                auto fragmented = original;
                for (int step = 0; step < 1024; ++step)
                {
                    const auto at = (frames - 1) * step / 1024;
                    storage::AudioEditTransaction edit(*fragmented);
                    for (int c = 0; c < channels; ++c)
                    {
                        edit.replaceChannel(c, at, 1, original.get(), c, at);
                    }
                    fragmented = edit.finish();
                }
                session.bindReadRevision(fragmented);
                store = imported.audio->blockStore();
            }
            else
            {
                initialize(session, frames);
                session.undoStore.attach(
                    std::filesystem::path(
                        request.at("root").get<std::string>()) /
                    "undo");
            }
            std::vector<double> deletes, undos, redos;
            const auto beforeIO =
                store ? store->ioBytes() : std::pair<uint64_t, uint64_t>{};
            const int repeats = owned ? 128 : 1;
            for (auto iteration : measurement)
            {
                (void)iteration;
                for (int i = 0; i < repeats; ++i)
                {
                    selection(state, 10001, 1);
                    auto started = Clock::now();
                    if (point)
                    {
                        editSample(&state, 0, 10001, sampleAt(10001, 0), .75f);
                    }
                    else
                    {
                        actions::audio::performDelete(&state);
                        finishRevisionCommands(&state);
                    }
                    deletes.push_back(elapsed(started));
                    require(session.document.getFrameCount() ==
                                frames - (point ? 0 : 1),
                            "Delete command did not commit");
                    started = Clock::now();
                    state.undo();
                    undos.push_back(elapsed(started));
                    require(session.document.getFrameCount() == frames,
                            "Undo command did not commit");
                    started = Clock::now();
                    state.redo();
                    redos.push_back(elapsed(started));
                    require(session.document.getFrameCount() ==
                                frames - (point ? 0 : 1),
                            "Redo command did not commit");
                    state.undo();
                }
                measurement.SetIterationTime(
                    (std::accumulate(deletes.begin(), deletes.end(), 0.0) +
                     std::accumulate(undos.begin(), undos.end(), 0.0) +
                     std::accumulate(redos.begin(), redos.end(), 0.0)) /
                    repeats / 1000.0);
            }
            if (store)
            {
                require(store->ioBytes() == beforeIO,
                        "Production edit/undo performed sample I/O");
            }
            const auto stats = [](std::vector<double> values)
            {
                std::sort(values.begin(), values.end());
                return Json{
                    {"median_ms", values[values.size() / 2]},
                    {"p99_ms",
                     values[std::min(values.size() - 1,
                                     std::size_t(values.size() * .99))]},
                    {"max_ms", values.back()}};
            };
            result["production_commands"] = {
                {point ? "sample" : "delete", stats(deletes)},
                {"undo", stats(undos)},
                {"redo", stats(redos)},
                {"repetitions", repeats},
                {"sample_io_bytes",
                 store ? store->ioBytes().first - beforeIO.first : 0}};
            captureMetrics();
            auto reader = session.getAudioReader();
            std::array<float, 16384> buffer;
            for (int c = 0; c < channels; ++c)
            {
                for (int64_t first = 0; first < frames; first += buffer.size())
                {
                    auto out = std::span(buffer).first(
                        std::min<int64_t>(buffer.size(), frames - first));
                    reader->readChannel(c, first, out);
                    for (std::size_t i = 0; i < out.size(); ++i)
                    {
                        require(out[i] == sampleAt(first + i, c),
                                "Production command undo lost samples");
                    }
                }
            }
            result["validated"] = true;
            return;
        }
        if (name == "open_owned_edit" || name == "open_owned_waveform")
        {
            const bool waveformCase = name == "open_owned_waveform";
            auto cache =
                std::make_shared<storage::DecodedBlockCache>(2 * 1024 * 1024);
            auto imported = file::importOwnedAudio(
                request.at("fixture").get<std::string>(),
                std::filesystem::path(request.at("root").get<std::string>()) /
                    "owned-audio",
                cache);
            auto original = storage::AudioEditRevision::from(imported.audio);
            auto base = original;
            // Fragment without changing samples. This prevents the benchmark
            // from measuring only the trivial one-leaf import representation.
            for (int step = 0; step < 1024; ++step)
            {
                const auto at = (frames - 1) * step / 1024;
                storage::AudioEditTransaction fragment(*base);
                for (int c = 0; c < channels; ++c)
                {
                    fragment.replaceChannel(c, at, 1, original.get(), c, at);
                }
                base = fragment.finish();
            }
            storage::AudioEditTransaction copy(*original);
            copy.trim(65530, 1);
            auto clipboard = copy.finish();
            std::vector<std::shared_ptr<const storage::AudioEditRevision>>
                history;
            history.reserve(128);
            if (waveformCase)
            {
                storage::AudioEditRevision::PeakWork setup;
                require(base->prepareWaveform(setup),
                        "Initial tree peaks missing");
            }
            double preparationMs = 0, maxPreparationMs = 0;
            uint64_t maxBoundarySamples = 0, maxPreparedNodes = 0;
            const auto beforeIO = imported.audio->blockStore()->ioBytes();
            uint64_t maxNodes = 0;
            double editMs = 0, undoMs = 0;
            for (auto iteration : measurement)
            {
                (void)iteration;
                for (int step = 0; step < 128; ++step)
                {
                    const auto editIO = imported.audio->blockStore()->ioBytes();
                    auto started = Clock::now();
                    storage::AudioEditTransaction edit(*base);
                    edit.erase(10001, 1);
                    edit.insert(10001, *clipboard);
                    auto changed = edit.finish();
                    editMs += elapsed(started);
                    require(imported.audio->blockStore()->ioBytes() == editIO,
                            "Reference edit accessed sample files");
                    if (waveformCase)
                    {
                        storage::AudioEditRevision::PeakWork work;
                        started = Clock::now();
                        require(changed->prepareWaveform(work),
                                "Edited peaks missing");
                        const auto duration = elapsed(started);
                        preparationMs += duration;
                        maxPreparationMs = std::max(maxPreparationMs, duration);
                        maxBoundarySamples =
                            std::max(maxBoundarySamples, work.boundarySamples);
                        maxPreparedNodes =
                            std::max(maxPreparedNodes, work.preparedNodes);
                    }
                    maxNodes = std::max(maxNodes, edit.allocatedIndexNodes());
                    history.push_back(changed);
                    started = Clock::now();
                    auto current = changed;
                    current = base; // Undo; history pins the edited revision.
                    benchmark::DoNotOptimize(current.get());
                    current = changed; // Redo.
                    benchmark::DoNotOptimize(current.get());
                    undoMs += elapsed(started);
                }
                measurement.SetIterationTime((editMs + undoMs + preparationMs) /
                                             1000.0);
            }
            if (!waveformCase)
            {
                require(imported.audio->blockStore()->ioBytes() == beforeIO,
                        "Reference edit/undo accessed sample files");
            }
            std::vector<gui::Peak> viewport;
            if (waveformCase)
            {
                require(maxBoundarySamples <= 4 * 254,
                        "Peak preparation scanned too much boundary audio");
                require(maxPreparedNodes < 128,
                        "Peak preparation rebuilt unaffected subtrees");
                viewport.resize(viewportWidth * channels);
                const auto queryIO = imported.audio->blockStore()->ioBytes();
                const auto started = Clock::now();
                storage::AudioEditRevision::PeakWork queries;
                for (int repeat = 0; repeat < 16; ++repeat)
                {
                    for (int c = 0; c < channels; ++c)
                    {
                        for (int pixel = 0; pixel < viewportWidth; ++pixel)
                        {
                            const auto first =
                                17 + (frames - 53) * pixel / viewportWidth;
                            const auto end = 17 + (frames - 53) * (pixel + 1) /
                                                      viewportWidth;
                            auto peak = history.back()->queryWaveformOverview(
                                c, first, end - first, queries);
                            require(peak.has_value(),
                                    "Prepared viewport returned pending");
                            viewport[c * viewportWidth + pixel] = *peak;
                        }
                    }
                }
                result["bounded_storage"]["overview_ms"] =
                    elapsed(started) / 16;
                result["bounded_storage"]["overview_tree_nodes"] =
                    queries.visitedNodes / 16;
                result["bounded_storage"]["overview_source_peaks"] =
                    queries.sourcePeaks / 16;
                result["bounded_storage"]["peak_prepare_mean_ms"] =
                    preparationMs / 128;
                result["bounded_storage"]["peak_prepare_max_ms"] =
                    maxPreparationMs;
                result["bounded_storage"]["max_boundary_samples"] =
                    maxBoundarySamples;
                result["bounded_storage"]["max_prepared_nodes"] =
                    maxPreparedNodes;
                require(imported.audio->blockStore()->ioBytes() == queryIO,
                        "Overview query read sample files");
            }
            require(maxNodes < 1024,
                    "Local edit copied too much index metadata");
            // Check every sample with bounded scratch after the edit timer.
            std::vector<float> block(storage::AudioBlockFrames);
            for (int c = 0; c < channels; ++c)
            {
                for (int64_t start = 0; start < frames; start += block.size())
                {
                    const auto count =
                        std::min<int64_t>(block.size(), frames - start);
                    history.back()->readChannel(
                        c, start, std::span<float>(block).first(count));
                    for (int64_t i = 0; i < count; ++i)
                    {
                        require(
                            block[i] ==
                                sampleAt(start + i == 10001 ? 65530 : start + i,
                                         c),
                            "Reference edit sample mismatch");
                        if (waveformCase && start + i >= 17 &&
                            start + i < frames - 36)
                        {
                            const auto pixel =
                                ((start + i - 17 + 1) * viewportWidth - 1) /
                                (frames - 53);
                            const auto peak =
                                viewport[c * viewportWidth + pixel];
                            require(peak.min <= block[i] &&
                                        peak.max >= block[i],
                                    "Waveform missed a sample extremum");
                        }
                    }
                }
            }
            // Check the navigation cost of the fragmented index too. Warm
            // both paths first and time bounded reads with identical samples.
            for (const auto count : {std::size_t{1024}, std::size_t{65536}})
            {
                std::vector<float> direct(count), indexed(count);
                const auto start = frames / 2;
                original->readChannel(0, start, direct);
                base->readChannel(0, start, indexed);
                require(direct == indexed, "Fragmented read mismatch");
                const auto beforeWarm = imported.audio->blockStore()->ioBytes();
                double directMs = 0, indexedMs = 0;
                for (int repetition = 0; repetition < 64; ++repetition)
                {
                    auto started = Clock::now();
                    original->readChannel(0, start, direct);
                    directMs += elapsed(started);
                    started = Clock::now();
                    base->readChannel(0, start, indexed);
                    indexedMs += elapsed(started);
                }
                require(imported.audio->blockStore()->ioBytes() == beforeWarm,
                        "Warm tree read accessed sample files");
                result["bounded_storage"]["direct_" + std::to_string(count) +
                                          "_frames_ms"] = directMs / 64;
                result["bounded_storage"]
                      ["fragmented_" + std::to_string(count) + "_frames_ms"] =
                          indexedMs / 64;
            }
            result["bounded_storage"].update(
                {{"edit_mean_ms", editMs / 128},
                 {"undo_redo_mean_ms", undoMs / 128},
                 {"max_edit_index_nodes", maxNodes},
                 {"index_height", base->indexHeight()},
                 {"edit_sample_bytes_read", 0},
                 {"edit_sample_bytes_written", 0}});
            result["milestones_ms"]["background_complete"] =
                editMs + undoMs + preparationMs;
            result["validated"] = true;
            return;
        }
        if (name == "open_owned_viewport" || name == "open_owned_viewport_sync")
        {
            constexpr uint64_t budget = 2 * 1024 * 1024;
            constexpr std::size_t maxWindow = 131072;
            auto cache = std::make_shared<storage::DecodedBlockCache>(budget);
            auto imported = file::importOwnedAudio(
                request.at("fixture").get<std::string>(),
                std::filesystem::path(request.at("root").get<std::string>()) /
                    "owned-audio",
                cache);
            const bool asynchronous = name == "open_owned_viewport";
            std::unique_ptr<storage::AsyncAudioReader> reader;
            if (asynchronous)
            {
                reader = std::make_unique<storage::AsyncAudioReader>(
                    imported.audio, maxWindow);
            }
            std::vector<double> dispatch, completion;
            double totalMs = 0;
            for (auto iteration : measurement)
            {
                (void)iteration;
                for (int step = 0; step < 64; ++step)
                {
                    const auto count = std::min<std::size_t>(
                        frames, std::array<std::size_t, 4>{
                                    1024, 65536, 8193, maxWindow}[step % 4]);
                    const int64_t start =
                        (frames - count) * ((step * 37) % 64) / 63;
                    std::vector<float> samples;
                    const auto started = Clock::now();
                    if (asynchronous)
                    {
                        const auto generation =
                            reader->submit(step % channels, start, count);
                        dispatch.push_back(elapsed(started));
                        for (;;)
                        {
                            if (auto window = reader->takePublished())
                            {
                                require(window->generation == generation,
                                        "Stale audio window published");
                                if (window->error)
                                {
                                    std::rethrow_exception(window->error);
                                }
                                samples = std::move(window->samples);
                                break;
                            }
                            require(elapsed(started) < 10000,
                                    "Timed out waiting for audio window");
                            std::this_thread::yield();
                        }
                    }
                    else
                    {
                        samples.resize(count);
                        storage::readAudioWindow(
                            *imported.audio, step % channels, start, samples,
                            []
                            {
                                return false;
                            });
                        dispatch.push_back(elapsed(started));
                    }
                    completion.push_back(elapsed(started));
                    totalMs += completion.back();
                    require(samples.size() == count,
                            "Audio window size mismatch");
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        require(samples[i] ==
                                    sampleAt(start + i, step % channels),
                                "Audio window sample mismatch");
                    }
                }
                measurement.SetIterationTime(totalMs / 1000.0);
            }
            if (reader)
            {
                reader->close();
                reader->waitUntilClosed();
            }
            const auto stats = cache->stats();
            require(stats.peakResidentBytes <= budget,
                    "Viewport cache exceeded budget");
            std::sort(dispatch.begin(), dispatch.end());
            std::sort(completion.begin(), completion.end());
            result["bounded_storage"] = {
                {"submit_p50_ms", dispatch[dispatch.size() / 2]},
                {"submit_max_ms", dispatch.back()},
                {"completion_p50_ms", completion[completion.size() / 2]},
                {"completion_max_ms", completion.back()},
                {"window_limit_bytes", maxWindow * sizeof(float)},
                {"peak_cached_sample_bytes", stats.peakResidentBytes},
                {"sample_bytes_read",
                 imported.audio->blockStore()->ioBytes().first},
                {"cache_hits", stats.hits},
                {"cache_misses", stats.misses}};
            result["milestones_ms"]["background_complete"] = totalMs;
            result["validated"] = true;
            return;
        }
        if (name == "open_owned")
        {
            constexpr uint64_t budget = 2 * 1024 * 1024;
            for (auto iteration : measurement)
            {
                (void)iteration;
                auto cache =
                    std::make_shared<storage::DecodedBlockCache>(budget);
                const auto started = Clock::now();
                auto imported = file::importOwnedAudio(
                    request.at("fixture").get<std::string>(),
                    std::filesystem::path(
                        request.at("root").get<std::string>()) /
                        "owned-audio",
                    cache, {}, {},
                    [&](waveform::ImportPreview)
                    {
                        if (result["milestones_ms"]["first_waveform"].is_null())
                        {
                            result["milestones_ms"]["first_waveform"] =
                                elapsed(started);
                        }
                    });
                result["milestones_ms"]["audio_available"] = elapsed(started);
                result["milestones_ms"]["waveform_complete"] = elapsed(started);
                result["milestones_ms"]["background_complete"] =
                    elapsed(started);
                measurement.SetIterationTime(elapsed(started) / 1000.0);
                require(imported.audio->shape().frames == frames,
                        "Owned import length mismatch");
                std::array<float, 1024> window;
                auto readStarted = Clock::now();
                imported.audio->readChannel(0, frames / 2 + 17, window);
                result["bounded_storage"]["cold_window_ms"] =
                    elapsed(readStarted);
                result["source_cloned"] = imported.sourceCloned;
                const auto beforeWarm =
                    imported.audio->blockStore()->ioBytes().first;
                readStarted = Clock::now();
                imported.audio->readChannel(0, frames / 2 + 17, window);
                result["bounded_storage"]["warm_window_ms"] =
                    elapsed(readStarted);
                require(imported.audio->blockStore()->ioBytes().first ==
                            beforeWarm,
                        "Warm read touched disk");
                // Validate every sample with bounded scratch, outside import
                // timing.
                std::vector<float> block(storage::AudioBlockFrames);
                for (int channel = 0; channel < channels; ++channel)
                {
                    for (int64_t start = 0; start < frames;
                         start += block.size())
                    {
                        const auto count =
                            std::min<int64_t>(block.size(), frames - start);
                        imported.audio->readChannel(
                            channel, start,
                            std::span<float>(block).first(count));
                        for (int64_t i = 0; i < count; ++i)
                        {
                            require(block[i] == sampleAt(start + i, channel),
                                    "Owned import sample mismatch");
                        }
                    }
                }
                const auto stats = cache->stats();
                require(stats.peakResidentBytes <= budget,
                        "Decoded cache exceeded budget");
                const auto io = imported.audio->blockStore()->ioBytes();
                result["bounded_storage"].update(
                    {{"budget_bytes", budget},
                     {"peak_cached_sample_bytes", stats.peakResidentBytes},
                     {"cache_hits", stats.hits},
                     {"cache_misses", stats.misses},
                     {"sample_bytes_read", io.first},
                     {"sample_bytes_written", io.second}});
            }
            result["validated"] = true;
            return;
        }
        const bool opening = name.starts_with("open_");
        const bool navigating =
            name.starts_with("scroll") || name.starts_with("zoom");
        const bool latency = name.starts_with("responsive_") || opening;
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
            finishRevisionCommands(&state);
            drain(state);
            state.getActiveDocumentSession().selection.reset();
            state.getActiveDocumentSession().cursor = 2000;
        }
        if (name == "undo" || name == "redo")
        {
            selection(state, start, 1000);
            actions::audio::performDelete(&state);
            finishRevisionCommands(&state);
            drain(state);
            if (name == "redo")
            {
                state.undo();
                drain(state);
            }
        }
        if (name == "open_decoded_cached")
        {
            state.importSampleCache = std::make_shared<storage::DecodedBlockCache>(64 * 1024 * 1024);
            const auto initialStart = Clock::now();
            actions::io::queueOpenFile(&state, request.at("fixture"));
            do
            {
                require(elapsed(initialStart) < request.value("timeout_seconds", 120) * 1000., "Initial import timed out");
                pump(state);
                if (!state.getActiveDocumentSession().openingPreview &&
                    state.getActiveDocumentSession().hasReadRevision() &&
                    result["decoded_cache"]["initial_editable_ms"].is_null())
                    result["decoded_cache"]["initial_editable_ms"] = elapsed(initialStart);
            } while (busy(state));
            result["decoded_cache"]["initial_cache_complete_ms"] = elapsed(initialStart);
            require(error.empty(), error);
            require(state.decodedImportCache && state.decodedImportCache->diskBytes() > 0,
                    "Decoded cache priming failed");
            actions::closeTabWithoutConfirmation(&state, 0);
            auto previous = std::weak_ptr(state.decodedImportCache);
            state.decodedImportCache.reset();
            const auto waiting = Clock::now();
            while (!previous.expired())
            {
                require(elapsed(waiting) < 10000, "Cache service release timed out");
                std::this_thread::yield();
            }
        }
        if (opening && name == "open_cached")
        {
            DocumentSession cached;
            auto loaded = file::legacy::loadAudioFile(request.at("fixture"));
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
                editSample(&state, 0, 9001, sampleAt(9001, 0), 0.25f);
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
                    finishRevisionCommands(&state);
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
                finishRevisionCommands(&state);
            }
            else if (name == "paste")
            {
                actions::audio::performPaste(&state);
                finishRevisionCommands(&state);
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
                finishRevisionCommands(&state);
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
            double maxPumpMs = 0;
            const auto deadline =
                Clock::now() +
                std::chrono::seconds(request.value("timeout_seconds", 120));
            do
            {
                require(Clock::now() < deadline, "Scenario timed out");
                const auto pumpStarted = Clock::now();
                pump(state);
                maxPumpMs = std::max(maxPumpMs, elapsed(pumpStarted));
                if (opening && !sawAudio &&
                    !state.getActiveDocumentSession().openingPreview &&
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
                if (opening && session.document.getChannelCount() > 0 &&
                    (session.hasReadRevision() ||
                     (session.openingCachedPeaks ||
                      (session.openingPeaks &&
                       session.openingPeaks->availableFrames() > 0) ||
                      session.getWaveformCache(0).builtSamplePrefixEnd() >
                          0)) &&
                    result["milestones_ms"]["first_waveform"].is_null())
                {
                    result["milestones_ms"]["first_waveform"] =
                        elapsed(started);
                }
                if (sawAudio &&
                    !result["milestones_ms"]["committed"].is_null() &&
                    !session.getWaveformCacheBuildProgress() &&
                    (session.hasReadRevision() ||
                     (session.getWaveformCache(0).levelsCount() > 0 &&
                      !session.getWaveformCache(0).hasDirtyBlocks() &&
                      session.getWaveformCache(1).levelsCount() > 0 &&
                      !session.getWaveformCache(1).hasDirtyBlocks())) &&
                    result["milestones_ms"]["waveform_complete"].is_null())
                {
                    result["milestones_ms"]["waveform_complete"] =
                        elapsed(started);
                }
                require(error.empty(), error);
            } while (busy(state) ||
                     (CUPUACU_BENCHMARK_SDL && !renderReady(state)));
            result["event_loop"]["max_iteration_ms"] = maxPumpMs;
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
        if (name == "open_decoded_cached")
        {
            require(state.decodedImportCache->diskHitCount() == 1, "Reopen did not use persisted decoded audio");
            require(state.getActiveDocumentSession().preservationSource->blockStore()->ioBytes().second == 0,
                    "Reopen wrote decoded sample blocks");
            result["decoded_cache"].update({{"disk_hits", state.decodedImportCache->diskHitCount()},
                {"disk_bytes", state.decodedImportCache->diskBytes()}, {"decoded_sample_bytes_written", 0}});
        }
        if (state.getActiveDocumentSession().hasReadRevision())
        {
            result["peak_process_rss_bytes_before_validation"] = peakRss();
            auto reader = state.getActiveDocumentSession().getAudioReader();
            require(reader->shape().frames == expectedFrames,
                    "Recovered frame count mismatch");
            std::array<float, 16384> samples;
            for (int c = 0; c < channels; ++c)
            {
                for (int64_t start = 0; start < expectedFrames;
                     start += samples.size())
                {
                    auto block = std::span(samples).first(std::min<int64_t>(
                        samples.size(), expectedFrames - start));
                    reader->readChannel(c, start, block);
                    for (std::size_t i = 0; i < block.size(); ++i)
                    {
                        require(std::abs(block[i] - expected(start + i, c)) <
                                    .000002f,
                                "Opened audio mismatch");
                    }
                }
            }
        }
        else
        {
            validateSamples(state.getActiveDocumentSession().document,
                            expectedFrames, expected);
        }
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
              {"first_waveform", nullptr},
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
