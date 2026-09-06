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

void editMetadataScenario(benchmark::State &measurement, int64_t frames)
{
    const auto edits = std::max<int64_t>(1, frames / 4096);
    const storage::AudioShape shape{1048576, 1, sampleRate,
                                    SampleFormat::FLOAT32};
    double editMs = 0, queryMs = 0, overviewMs = 0;
    storage::EditTree::Stats stats{};
    for (auto iteration : measurement)
    {
        (void)iteration;
        auto current = storage::AudioEditRevision::silence(shape);
        std::vector<std::shared_ptr<const storage::AudioEditRevision>> history;
        auto began = Clock::now();
        for (int64_t i = 0; i < edits; ++i)
        {
            storage::AudioEditTransaction transaction(*current);
            transaction.replaceChannel(0, i * 7919 % shape.frames, 1, nullptr,
                                       0, 0, float(i + 1));
            current = transaction.finish();
            history.push_back(current);
        }
        editMs = elapsed(began);
        stats = current->indexStats();
        require(stats.residentBytes <= 128 * 1024,
                "Edit node buffers grew with history");
        began = Clock::now();
        std::array<float, 1> value;
        for (int64_t i = 0; i < edits; ++i)
        {
            history[i]->readChannel(0, i * 7919 % shape.frames, value);
            require(value[0] == float(i + 1), "Paged history sample mismatch");
        }
        queryMs = elapsed(began);
        storage::AudioEditRevision::PeakWork peakWork;
        require(current->prepareWaveform(peakWork), "Missing edited summaries");
        const auto draw = [&]
        {
            for (int x = 0; x < 1024; ++x)
            {
                require(bool(current->queryWaveformOverview(0, x * 1024, 1024,
                                                            peakWork)),
                        "Missing edited overview pixel");
            }
        };
        draw();
        began = Clock::now();
        draw();
        overviewMs = elapsed(began);

        measurement.SetIterationTime((editMs + queryMs) / 1000.);
    }
    result["validated"] = true;
    result["milestones_ms"]["background_complete"] = editMs + queryMs;
    result["edit_metadata"] = {{"edits", edits},
                               {"live_nodes", stats.liveNodes},
                               {"record_buffers_bytes", stats.residentBytes},
                               {"edit_ms", editMs},
                               {"history_queries_ms", queryMs},
                               {"warm_overview_ms", overviewMs}};
}

void legacyMetadataScenario(benchmark::State &measurement, int64_t frames,
                            bool paged)
{
    // Definite-length version-one array, generated without a setup DOM so RSS
    // reflects parsing rather than fixture construction. Labels scale rows.
    const uint64_t count = std::max<int64_t>(1, frames / 16);
    const auto root =
        std::filesystem::path(request.at("root").get<std::string>());
    const auto path = root / "legacy-metadata.cbor";
    std::ofstream fixture(path, std::ios::binary);
    auto integer = [&](uint8_t major, uint64_t value)
    {
        if (value < 24)
        {
            fixture.put(char((major << 5) | value));
        }
        else
        {
            const int bytes = value <= 255          ? 1
                              : value <= 65535      ? 2
                              : value <= UINT32_MAX ? 4
                                                    : 8;
            fixture.put(char((major << 5) | (bytes == 1   ? 24
                                             : bytes == 2 ? 25
                                             : bytes == 4 ? 26
                                                          : 27)));
            for (int i = bytes - 1; i >= 0; --i)
            {
                fixture.put(char(value >> (8 * i)));
            }
        }
    };
    integer(4, count);
    for (uint64_t i = 0; i < count; ++i)
    {
        integer(4, 5);
        integer(0, i);
        integer(0, 1);
        integer(0, 0);
        integer(0, i);
        fixture.put(char(i % 2 ? 0xf5 : 0xf4));
    }
    fixture.close();
    require(bool(fixture), "Cannot generate legacy metadata");
    auto memory = storage::defaultDecodedBlockCache();
    memory->setByteBudget(8 * 1024 * 1024);
    const auto bytes = std::filesystem::file_size(path);
    double ms = 0;
    for (auto iteration : measurement)
    {
        (void)iteration;
        std::ifstream in(path, std::ios::binary);
        auto begin = Clock::now();
        uint64_t at = 0;
        const auto visit = [&](const Json &row)
        {
            require(row.at(0).get<uint64_t>() == at &&
                        row.at(3).get<uint64_t>() == at &&
                        row.at(4).get<bool>() == bool(at % 2),
                    "Legacy metadata mismatch");
            ++at;
        };
        if (paged)
        {
            storage::PagedCbor spool;
            const auto value = spool.read(path, 0, bytes, [] {});
            if (value.is_array())
            {
                for (const auto &row : value)
                {
                    visit(row);
                }
            }
            else
            {
                spool.visit(value, visit, [] {});
            }
        }
        else
        {
            const auto value = Json::from_cbor(in);
            for (const auto &row : value)
            {
                visit(row);
            }
        }
        ms = elapsed(begin);
        require(at == count, "Incomplete legacy metadata visit");
        measurement.SetIterationTime(ms / 1000.);
    }
    const auto stats = memory->stats();
    require(!paged || (stats.peakManagedBytes <= 8 * 1024 * 1024 &&
                       stats.workingBytes == 0),
            "Legacy metadata staging exceeded or retained its budget");
    result["validated"] = true;
    result["milestones_ms"]["background_complete"] = ms;
    result["legacy_metadata"] = {{"records", count},
                                 {"encoded_bytes", bytes},
                                 {"peak_managed_bytes", stats.peakManagedBytes},
                                 {"peak_rss_bytes", peakRss()},
                                 {"read_visit_ms", ms}};
}
