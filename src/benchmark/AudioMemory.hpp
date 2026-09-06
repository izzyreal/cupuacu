// Included inside the benchmark implementation namespace.
void audioMemoryScenario(benchmark::State &measurement, int64_t frames)
{
    constexpr uint64_t budget = 8 * 1024 * 1024;
    auto cache = std::make_shared<storage::DecodedBlockCache>(budget);
    const auto root =
        std::filesystem::path(request.at("root").get<std::string>());
    std::vector<std::unique_ptr<storage::AudioBlockStore>> stores;
    for (int i = 0; i < 4; ++i)
    {
        stores.push_back(std::make_unique<storage::AudioBlockStore>(
            root / ("memory-" + std::to_string(i))));
    }
    std::vector<float> samples(storage::AudioBlockFrames, .25f);
    std::vector<std::pair<int, storage::AudioBlock>> blocks;
    for (int64_t remaining = frames * 2; remaining > 0;)
    {
        const auto count = std::min<int64_t>(remaining, samples.size());
        const int index = blocks.size() % stores.size();
        blocks.emplace_back(index,
                            stores[index]->append(
                                std::span<const float>(samples).first(count)));
        remaining -= count;
    }
    for (auto &store : stores)
    {
        store->flush();
    }
    std::array<float, 31> output{};
    double scanMs = 0, warmMs = 0, trimMs = 0;
    for (auto iteration : measurement)
    {
        (void)iteration;
        auto began = Clock::now();
        for (auto [index, block] : blocks)
        {
            cache->read(*stores[index], block, 0, output);
            require(output.front() == .25f && output.back() == .25f,
                    "Shared cache sample mismatch");
        }
        scanMs = elapsed(began);
        const auto [index, block] = blocks.back();
        began = Clock::now();
        for (int i = 0; i < 10000; ++i)
        {
            cache->read(*stores[index], block, 0, output);
        }
        warmMs = elapsed(began);
        began = Clock::now();
        auto scratch = cache->reserveScratch(budget / 2);
        require(cache->stats().residentBytes <= budget / 2,
                "Scratch did not displace cache");
        cache->setPressure(2);
        require(cache->stats().residentBytes == 0,
                "Pressure did not trim cache");
        scratch.reset();
        cache->setPressure(0);
        trimMs = elapsed(began);
        measurement.SetIterationTime((scanMs + warmMs + trimMs) / 1000.);
    }
    const auto stats = cache->stats();
    require(stats.peakManagedBytes <= budget, "Shared audio budget exceeded");
    result["validated"] = true;
    result["milestones_ms"]["background_complete"] = scanMs + warmMs + trimMs;
    result["audio_memory"] = {{"stores", stores.size()},
                              {"blocks", blocks.size()},
                              {"budget_bytes", budget},
                              {"peak_managed_bytes", stats.peakManagedBytes},
                              {"scan_ms", scanMs},
                              {"warm_10000_reads_ms", warmMs},
                              {"trim_ms", trimMs},
                              {"hits", stats.hits},
                              {"misses", stats.misses},
                              {"evictions", stats.evictions}};
}

void workingMemoryScenario(benchmark::State &measurement, int64_t frames)
{
    constexpr uint64_t budget = 8 * 1024 * 1024;
    auto memory = storage::defaultDecodedBlockCache();
    memory->setByteBudget(budget);
    const auto root =
        std::filesystem::path(request.at("root").get<std::string>());
    const storage::AudioShape shape{frames, 2, sampleRate,
                                    SampleFormat::FLOAT32};
    double importMs = 0, viewsMs = 0, effectMs = 0, transportMs = 0;
    storage::DecodedBlockCache::Stats during{};
    for (auto iteration : measurement)
    {
        (void)iteration;
        auto began = Clock::now();
        {
            auto store = std::make_shared<storage::AudioBlockStore>(
                root / "working-memory");
            auto progressive =
                std::make_shared<waveform::ProgressivePeaks>(shape, memory);
            waveform::StreamingPeakBuilder peaks(shape, memory, {},
                                                 progressive);
            storage::AudioRevisionBuilder builder(
                shape, store, memory,
                [&](int64_t first,
                    std::span<
                        const storage::AudioRevisionBuilder::PendingChannel>
                        channels,
                    uint32_t count)
                {
                    peaks.appendFrom(
                        shape, first + count,
                        [&](int c, int64_t at, std::span<float> out)
                        {
                            std::copy_n(channels[c].data() + at - first,
                                        out.size(), out.data());
                        });
                });
            auto inputMemory = storage::reserveWorking(
                4096 * 2 * sizeof(float), storage::MemoryUse::Import);
            std::vector<float> input(4096 * 2, .25f);
            for (int64_t first = 0; first < frames; first += 4096)
            {
                builder.appendInterleaved(std::span(input).first(
                    std::min<int64_t>(4096, frames - first) * 2));
            }
            auto revision = storage::AudioEditRevision::from(
                builder.finish({}, peaks.finish()));
            importMs = elapsed(began);
            waveform::ViewportSource source;
            source.audio = revision;
            source.memory = memory;
            source.overview = [&](int c, int64_t first, int64_t count)
            {
                storage::AudioEditRevision::PeakWork work;
                return revision->queryWaveformOverview(c, first, count, work);
            };
            source.prepare = [&](const auto &cancel)
            {
                storage::AudioEditRevision::PeakWork work;
                return revision->prepareWaveform(work, cancel);
            };
            began = Clock::now();
            std::vector<waveform::ViewportData> views;
            for (int i = 0; i < 16; ++i)
            {
                auto view = waveform::WaveformViewport::compute(
                    source, {i % 2, (frames / 32) * i, i % 2 ? .5 : 127., 1024},
                    []
                    {
                        return false;
                    });
                require(bool(view) && !view->pending,
                        "Missing managed waveform view");
                views.push_back(std::move(*view));
            }
            viewsMs = elapsed(began);
            began = Clock::now();
            actions::effects::BackgroundEffectRequest effect;
            effect.frameCount = 1024;
            effect.targetChannels = {0, 1};
            auto effected = actions::effects::computeRevisionEffect(
                effect, revision, root / "memory-effect", {});
            std::array<float, 1> checkSample{};
            effected->afterRevision->readChannel(0, 0, checkSample);
            require(checkSample[0] == .25f,
                    "Budgeted effect changed constant samples");
            effectMs = elapsed(began);
            began = Clock::now();
            playback::ReadAhead playback(revision);
            storage::RecordingWriter recording(revision, 0,
                                               root / "memory-recording");
            recording.startWorker();
            audio::RecordedChunk chunk{};
            chunk.frameCount = 256;
            chunk.channelCount = 2;
            chunk.interleavedSamples.fill(.5f);
            require(recording.submit(chunk),
                    "Budgeted recording rejected input");
            recording.finish();
            const auto deadline = Clock::now() + std::chrono::seconds(5);
            while (!playback.isReady(0) || !recording.snapshot().completed)
            {
                require(!playback.failed() && Clock::now() < deadline,
                        "Budgeted transport did not complete");
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            float left = 0, right = 0;
            require(playback.readStereo(0, left, right) && left == .25f &&
                        right == .25f,
                    "Budgeted playback sample mismatch");
            playback.endCallback();
            auto recorded = recording.snapshot();
            recorded.audio->readChannel(0, 0, checkSample);
            require(recorded.error.empty() && checkSample[0] == .5f,
                    "Budgeted recording did not publish samples: " +
                        recorded.error);
            transportMs = elapsed(began);
            during = memory->stats();
            require(
                during.workingByUse[unsigned(storage::MemoryUse::Transport)] >
                    0,
                "Transport queues lost their reservations");
            playback.close();
            while (!playback.finished())
            {
                require(Clock::now() < deadline, "Playback cleanup timed out");
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            require(
                during.workingByUse[unsigned(storage::MemoryUse::Viewport)] > 0,
                "Published views lost their reservation");
            measurement.SetIterationTime(
                (importMs + viewsMs + effectMs + transportMs) / 1000.);
        }
    }
    const auto releaseDeadline = Clock::now() + std::chrono::seconds(5);
    while (memory->stats().workingBytes && Clock::now() < releaseDeadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto final = memory->stats();
    require(final.peakManagedBytes <= budget,
            "Aggregate working budget exceeded");
    require(final.workingBytes == 0,
            "Released sources retained working memory");
    result["validated"] = true;
    result["milestones_ms"]["background_complete"] =
        importMs + viewsMs + effectMs + transportMs;
    result["working_memory"] = {
        {"budget_bytes", budget},
        {"peak_managed_bytes", final.peakManagedBytes},
        {"retained_working_bytes", during.workingBytes},
        {"index_bytes",
         during.workingByUse[unsigned(storage::MemoryUse::Index)]},
        {"peak_bytes",
         during.workingByUse[unsigned(storage::MemoryUse::Peaks)]},
        {"viewport_bytes",
         during.workingByUse[unsigned(storage::MemoryUse::Viewport)]},
        {"transport_bytes",
         during.workingByUse[unsigned(storage::MemoryUse::Transport)]},
        {"effect_ms", effectMs},
        {"transport_ms", transportMs},
        {"released_working_bytes", final.workingBytes},
        {"import_ms", importMs},
        {"views_ms", viewsMs}};
}
