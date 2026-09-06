// Included inside the benchmark namespace to share the production event pump,
// deterministic fixture, timing helpers and JSON protocol.
void largeFileWorkflow(benchmark::State &measurement)
{
    const int64_t frames = request.at("frames");
    constexpr uint64_t budget = 64 * 1024 * 1024;
    State state;
    state.paths =
        std::make_unique<BenchPaths>(request.at("root").get<std::string>());
    state.importSampleCache =
        std::make_shared<storage::DecodedBlockCache>(budget);
    const auto root =
        std::filesystem::path(request.at("root").get<std::string>());
    const auto fixture = request.at("fixture").get<std::string>();
    const int tabCount = request.value("tab_count", 1);
    const auto deadline =
        Clock::now() +
        std::chrono::seconds(request.value("timeout_seconds", 120));
    auto checkedPump = [&]
    {
        require(Clock::now() < deadline, "Workflow timed out");
        const bool opening = bool(state.backgroundOpenJob);
        const auto begin = Clock::now();
        pump(state);
        const auto duration = elapsed(begin);
        if (duration > 10)
        {
            result["workflow_slow_pumps"].push_back(
                {{"ms", duration},
                 {"opening_before", opening},
                 {"opening_after", bool(state.backgroundOpenJob)},
                 {"tabs", state.tabs.size()}});
        }
    };
    std::vector<std::filesystem::path> inputs;
    for (int tab = 0; tab < tabCount; ++tab)
    {
        auto input =
            root / ("input-" + std::to_string(tab) +
                    std::filesystem::path(fixture).extension().string());
        std::filesystem::copy_file(fixture, input);
        inputs.push_back(std::move(input));
    }
    for (auto iteration : measurement)
    {
        (void)iteration;
        const auto started = Clock::now();
        Probes probes(state);
        for (int tab = 0; tab < tabCount; ++tab)
        {
            const auto &input = inputs[tab];
            const auto tabStarted = Clock::now();
            actions::io::queueOpenFile(&state, input.string());
            bool committed = false;
            do
            {
                checkedPump();
                auto &session = state.getActiveDocumentSession();
                if (tab == 0)
                {
                    auto mark = [&](const char *key, bool ready)
                    {
                        if (ready && result["milestones_ms"][key].is_null())
                        {
                            result["milestones_ms"][key] = elapsed(started);
                        }
                    };
                    mark("metadata",
                         session.document.getFrameCount() == frames);
                    mark("first_playable",
                         session.openingAudio &&
                             session.openingAudio->availableFrames() > 0);
                    mark("first_waveform",
                         session.openingCachedPeaks ||
                             (session.openingPeaks &&
                              session.openingPeaks->availableFrames() > 0) ||
                             (session.document.getChannelCount() &&
                              session.getWaveformCache(0)
                                      .builtSamplePrefixEnd() > 0));
                }
                committed = !session.openingPreview &&
                            session.hasReadRevision() &&
                            session.currentFile == input.string();
                if (committed && tab == 0)
                {
                    result["milestones_ms"]["editable"] = elapsed(started);
                    // A tiny import may finish between two UI observations.
                    for (auto key : {"first_playable", "first_waveform"})
                    {
                        if (result["milestones_ms"][key].is_null())
                        {
                            result["milestones_ms"][key] = elapsed(started);
                        }
                    }
                }
            } while (!committed);
            result["workflow_import_tabs_ms"].push_back(elapsed(tabStarted));
        }
        result["workflow"]["all_tabs_editable_ms"] = elapsed(started);
        probes.finish();
        drain(state);
        result["milestones_ms"]["peak_persistence_complete"] = elapsed(started);
        result["workflow"]["import_peak_rss_bytes_before_validation"] =
            peakRss();
        require(state.tabs.size() == std::size_t(tabCount),
                "Unexpected import tab count");
        state.activeTabIndex = 0;
        auto original = state.tabs[0].session.getEditRevision();
        auto store = state.tabs[0].session.preservationSource->blockStore();
        // Match command microbenchmarks: isolate edit publication from autosave
        // and persistent clipboard jobs; normal paths resume for save/reopen.
        auto paths = std::move(state.paths);
        auto &session = state.tabs[0].session;
        session.document.addMarker(13, "first");
        session.document.addMarker(frames / 2 + 3, "middle");
        session.document.addMarker(frames - 11, "last");
        const auto markers = session.document.getMarkers();
        const auto beforeIO = store->ioBytes();
        auto timed = [&](const char *name, auto operation)
        {
            const auto begin = Clock::now();
            operation();
            result["workflow"][name] = elapsed(begin);
        };
        auto pointEdit = [&]
        {
            state.addAndDoUndoable(
                std::make_shared<actions::audio::SetSampleValue>(
                    &state, 0, 9001, sampleAt(9001, 0), .25f));
        };
        timed("point_edit_ms", pointEdit);
        timed("undo_ms",
              [&]
              {
                  state.undo();
              });
        require(session.getEditRevision() == original,
                "Point undo lost revision");
        timed("redo_ms",
              [&]
              {
                  state.redo();
              });
        state.undo();
        selection(state, 13, 1000);
        timed("delete_ms",
              [&]
              {
                  actions::audio::performDelete(&state);
                  finishRevisionCommands(&state);
              });
        require(session.document.getFrameCount() == frames - 1000,
                "Delete failed");
        state.undo();
        state.redo();
        state.undo();
        selection(state, 10001, 1000);
        timed("cut_ms",
              [&]
              {
                  actions::audio::performCut(&state);
                  finishRevisionCommands(&state);
              });
        require(session.document.getFrameCount() == frames - 1000,
                "Cut failed");
        state.undo();
        auto clipboard = state.clipboard.getAudioRevision();
        require(bool(clipboard), "Cut lost clipboard");
        selection(state, 20001, 0);
        timed("paste_ms",
              [&]
              {
                  actions::audio::performPaste(&state);
                  finishRevisionCommands(&state);
              });
        require(session.document.getFrameCount() == frames + 1000,
                "Paste failed");
        state.undo();
        state.redo();
        state.undo();
        require(session.getEditRevision() == original &&
                    session.document.getMarkers() == markers,
                "History restoration failed");
        timed("split_ms",
              [&]
              {
                  require(actions::markers::splitByMarkers(&state),
                          "Split failed");
                  finishRevisionCommands(&state);
              });
        require(state.tabs.size() == std::size_t(tabCount + 2),
                "Split tab count");
        for (int i = 1; i <= 2; ++i)
        {
            auto &part = state.tabs[i].session;
            require(part.document.getFrameCount() ==
                        markers[i].frame - markers[i - 1].frame,
                    "Split duration mismatch");
            require(part.document.getMarkers().front().frame == 0 &&
                        part.document.getMarkers().back().frame ==
                            part.document.getFrameCount(),
                    "Split markers mismatch");
        }
        require(store->ioBytes() == beforeIO,
                "Local edits copied/read source samples");
        result["workflow"]["local_edit_sample_io_bytes"] = 0;
        // Close split tabs without disturbing clipboard or the original
        // revision.
        actions::closeTabWithoutConfirmation(&state, 2);
        actions::closeTabWithoutConfirmation(&state, 1);
        require(state.clipboard.getAudioRevision() == clipboard,
                "Close lost clipboard");
        pointEdit();
        state.paths = std::move(paths);

        // Same session snapshot and asynchronous viewport worker used by the
        // UI. Guard the reader to catch any accidental disk access on this
        // thread.
        class GuardedReader final : public storage::AudioReader
        {
            std::shared_ptr<const storage::AudioReader> reader;
            std::thread::id ui;

        public:
            explicit GuardedReader(
                std::shared_ptr<const storage::AudioReader> source)
                : reader(std::move(source)), ui(std::this_thread::get_id())
            {
            }
            storage::AudioShape shape() const override
            {
                return reader->shape();
            }
            void readChannel(int channel, int64_t first,
                             std::span<float> out) const override
            {
                require(std::this_thread::get_id() != ui,
                        "UI performed viewport sample read");
                reader->readChannel(channel, first, out);
            }
        };
        auto source = *state.tabs[0].session.getViewportSource();
        source.audio = std::make_shared<GuardedReader>(source.audio);
        waveform::WaveformViewport worker(std::move(source));
        std::vector<double> warm, cold, dispatch;
        for (int pass = 0; pass < 33; ++pass)
        {
            for (int step = 0; step < 32; ++step)
            {
                const double spp = std::array<double, 4>{
                    .25, 7.3, 127.9, double(frames) / 1024}[step % 4];
                const int64_t offset =
                    step % 4 == 3
                        ? 0
                        : int64_t(std::max(0.0, frames - 1024 * spp - 4)) *
                              ((step * 37) % 32) / 31;
                const auto start = Clock::now();
                const auto generation =
                    worker.submit({step % 2, offset, spp, 1024});
                dispatch.push_back(elapsed(start));
                std::optional<waveform::WaveformViewport::Result> ready;
                while (!(ready = worker.takePublished()))
                {
                    require(elapsed(start) < 10000, "Viewport timed out");
                    std::this_thread::yield();
                }
                (pass ? warm : cold).push_back(elapsed(start));
                require(ready->generation == generation,
                        "Stale viewport publication");
                if (ready->error)
                {
                    std::rethrow_exception(ready->error);
                }
                require(ready->value && !ready->value->pending,
                        "Pending completed viewport");
            }
        }
        std::sort(warm.begin(), warm.end());
        result["workflow"]["viewport_warm_p99_ms"] =
            warm[std::size_t(std::ceil(warm.size() * .99)) - 1];
        result["workflow"]["viewport_cold_max_ms"] =
            *std::max_element(cold.begin(), cold.end());
        result["workflow"]["viewport_dispatch_max_ms"] =
            *std::max_element(dispatch.begin(), dispatch.end());
        result["workflow"]["sample_cache_peak_bytes_before_validation"] =
            state.importSampleCache->stats().peakResidentBytes;
        result["workflow"]["sample_cache_budget_bytes"] = budget;
        worker.close();
        worker.waitUntilClosed();
        // Float WAV makes full round-trip equality meaningful; PCM16 export
        // intentionally quantizes. Preservation saves have separate coverage.
        const auto output = root / "saved.wav";
        timed("save_ms",
              [&]
              {
                  require(actions::io::queueSaveAs(
                              &state, output.string(),
                              *file::defaultExportSettingsForPath(
                                  output, SampleFormat::FLOAT32)),
                          "Save rejected");
                  drain(state);
              });
        require(std::filesystem::exists(output) &&
                    !state.tabs[0].session.revisionHasUnsavedChanges(),
                "Save failed");
        // Close before reopening; clipboard must retain independent source
        // data.
        auto saved = state.tabs[0].session.getAudioReader();
        actions::closeTabWithoutConfirmation(&state, 0);
        require(state.clipboard.getAudioRevision() == clipboard,
                "Source close lost clipboard");
        timed("reopen_ms",
              [&]
              {
                  actions::io::queueOpenFile(&state, output.string());
                  drain(state);
              });
        require(state.getActiveDocumentSession().document.getMarkers() ==
                    markers,
                "Save/reopen marker mismatch");
        result["milestones_ms"]["background_complete"] = elapsed(started);
        measurement.SetIterationTime(elapsed(started) / 1000.0);
        captureMetrics();
        // Full validation is deliberately outside timings and import-memory
        // capture.
        std::array<float, 16384> block;
        for (auto reader :
             {saved, state.getActiveDocumentSession().getAudioReader()})
        {
            require(reader->shape().frames == frames,
                    "Reopen duration mismatch");
            for (int c = 0; c < channels; ++c)
            {
                for (int64_t first = 0; first < frames; first += block.size())
                {
                    auto values = std::span(block).first(
                        std::min<int64_t>(block.size(), frames - first));
                    reader->readChannel(c, first, values);
                    for (std::size_t i = 0; i < values.size(); ++i)
                    {
                        if (values[i] != (c == 0 && first + int64_t(i) == 9001
                                              ? .25f
                                              : sampleAt(first + i, c)))
                        {
                            throw std::runtime_error(
                                "Workflow sample mismatch at " +
                                std::to_string(first + i));
                        }
                    }
                }
            }
        }
        require(state.importSampleCache->stats().peakResidentBytes <= budget,
                "Shared sample cache exceeded budget");
        result["workflow"]["sample_cache_peak_bytes_after_validation"] =
            state.importSampleCache->stats().peakResidentBytes;
        result["workflow"]["warm_viewport_target_met"] =
            result["workflow"]["viewport_warm_p99_ms"].get<double>() < 16.7;
        auto latency = result["event_latency"]["p99_ms"];
        result["workflow"]["event_latency_p99_target_met"] =
            latency.is_number() ? Json(latency.get<double>() < 50)
                                : Json(nullptr);
        require(result["workflow"]["warm_viewport_target_met"].get<bool>(),
                "Warm viewport p99 exceeded 16.7 ms");
        require((latency.is_number()
                     ? latency.get<double>()
                     : result["event_latency"]["max_ms"].get<double>()) < 50,
                "Main-loop event latency exceeded 50 ms");
        result["validated"] = true;
    }
}
