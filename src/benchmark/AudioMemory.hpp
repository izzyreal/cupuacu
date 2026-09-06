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
