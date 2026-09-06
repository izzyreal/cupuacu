// Included inside the benchmark implementation namespace. No audio fixture:
// scale the real index independently of the samples it addresses.
void indexPagingScenario(benchmark::State &measurement, int64_t frames,
                         bool paged)
{
    using Record = std::array<uint64_t, 5>;
    const auto count = std::max<uint64_t>(1, uint64_t(frames) / 8);
    storage::RecordIndex<Record> disk;
    std::vector<Record> resident;
    const auto value = [](uint64_t i) -> Record
    {
        return {i * 8, 8, 17 + i % 3, i * 8, i % 2};
    };
    double appendMs = 0, scanMs = 0, randomMs = 0;
    uint64_t sum = 0;
    for (auto iteration : measurement)
    {
        (void)iteration;
        auto began = Clock::now();
        for (uint64_t i = 0; i < count; ++i)
        {
            if (paged)
            {
                disk.push_back(value(i));
            }
            else
            {
                resident.push_back(value(i));
            }
        }
        if (paged)
        {
            disk.seal();
        }
        appendMs = elapsed(began);
        began = Clock::now();
        for (uint64_t i = 0; i < count; ++i)
        {
            const auto row = paged ? disk[i] : resident[i];
            sum += row[0];
        }
        scanMs = elapsed(began);
        began = Clock::now();
        for (uint64_t i = 0; i < 1024; ++i)
        {
            const auto at = (i * 104729) % count;
            require((paged ? disk[at] : resident[at]) == value(at),
                    "Index lookup mismatch");
        }
        randomMs = elapsed(began);
        measurement.SetIterationTime((appendMs + scanMs + randomMs) / 1000.);
    }
    require(sum == count * (count - 1) * 4, "Index scan mismatch");
    const auto stats = disk.stats();
    require(!paged || stats.residentBytes <= 512 * sizeof(Record),
            "Index residency grew beyond two pages");
    result["validated"] = true;
    result["milestones_ms"]["background_complete"] =
        appendMs + scanMs + randomMs;
    result["index_paging"] = {
        {"records", count},
        {"resident_bytes",
         paged ? stats.residentBytes : resident.capacity() * sizeof(Record)},
        {"disk_bytes", stats.diskBytes},
        {"read_bytes", stats.readBytes},
        {"append_ms", appendMs},
        {"scan_ms", scanMs},
        {"random_1024_ms", randomMs}};
}

void indexArchiveScenario(benchmark::State &measurement, int64_t frames)
{
    // Worst-case metadata (one run per sample), generated in small chunks.
    const auto count = std::max<int64_t>(1, frames / 8);
    const auto root =
        std::filesystem::path(request.at("root").get<std::string>());
    const storage::AudioShape shape{count, 1, sampleRate,
                                    SampleFormat::FLOAT32};
    auto cache =
        std::make_shared<storage::DecodedBlockCache>(storage::AudioBlockBytes);
    auto store =
        std::make_shared<storage::AudioBlockStore>(root / "index-audio");
    storage::AudioRevisionBuilder builder(shape, store, cache);
    std::array<float, 1024> samples;
    samples.fill(.25f);
    std::array<audio::SampleProvenance, 1024> provenance;
    std::array<uint8_t, 1024> dirty;
    for (int64_t first = 0; first < count; first += 1024)
    {
        const auto take = std::min<int64_t>(1024, count - first);
        for (int64_t i = 0; i < take; ++i)
        {
            provenance[i] = {17, first + i};
            dirty[i] = (first + i) % 2;
        }
        builder.appendChannelMetadata(0, first,
                                      std::span(provenance).first(take),
                                      std::span(dirty).first(take));
        builder.appendInterleaved(std::span(samples).first(take));
    }
    auto source = builder.finish();
    auto archive = storage::RevisionArchive::open(root / "index-manifest");
    std::shared_ptr<const storage::AudioRevision> restored;
    double saveMs = 0, loadMs = 0;
    for (auto iteration : measurement)
    {
        (void)iteration;
        auto began = Clock::now();
        const auto id = archive->saveSource(source);
        archive->commit({{"source", id}});
        saveMs = elapsed(began);
        began = Clock::now();
        archive->readManifest();
        restored = archive->loadSource(id);
        loadMs = elapsed(began);
        measurement.SetIterationTime((saveMs + loadMs) / 1000.);
    }
    for (int64_t first = 0; first < count; first += 1024)
    {
        const auto take = std::min<int64_t>(1024, count - first);
        restored->readLegacyMetadata(0, first,
                                     std::span(provenance).first(take),
                                     std::span(dirty).first(take));
        for (int64_t i = 0; i < take; ++i)
        {
            require(provenance[i].sourceId == 17 &&
                        provenance[i].frameIndex == first + i &&
                        dirty[i] == (first + i) % 2,
                    "Recovered index metadata mismatch");
        }
    }
    const auto stats = restored->indexStats();
    require(stats.residentBytes <= 512 * (40 + sizeof(storage::AudioBlock)),
            "Recovered index residency grew");
    result["validated"] = true;
    result["milestones_ms"]["background_complete"] = saveMs + loadMs;
    result["index_paging"] = {{"records", count},
                              {"resident_bytes", stats.residentBytes},
                              {"disk_bytes", stats.diskBytes},
                              {"archive_bytes", archive->stats.metadataBytes},
                              {"save_ms", saveMs},
                              {"load_ms", loadMs},
                              {"peak_rss_bytes", peakRss()}};
}
