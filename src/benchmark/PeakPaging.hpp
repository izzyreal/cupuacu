// Included inside the benchmark implementation namespace.
void peakPagingScenario(benchmark::State &measurement, int64_t frames,
                        bool disk, bool streaming = false)
{
    storage::AudioShape shape{frames, 2, 48000, SampleFormat::FLOAT32};
    const auto count = frames / 128 + (frames % 128 != 0);
    std::vector<std::vector<gui::PeakLevel>> levels(2);
    if (!streaming)
    {
        for (auto &channel : levels)
        {
            channel.resize(1);
            channel[0].resize(count);
            for (int64_t i = 0; i < count; ++i)
            {
                channel[0].set(i, {-.5f, .75f});
            }
        }
    }
    auto cache = std::make_shared<storage::DecodedBlockCache>(1024 * 1024);
    std::shared_ptr<const waveform::SourcePeaks> peaks;
    const auto began = Clock::now();
    if (streaming)
    {
        peaks = waveform::SourcePeaks::createStreaming(
            shape,
            [](int, uint64_t, std::span<waveform::Peak> out)
            {
                std::fill(out.begin(), out.end(), waveform::Peak{-.5f, .75f});
            },
            cache);
    }
    else if (disk)
    {
        peaks =
            waveform::SourcePeaks::createPaged(shape, std::move(levels), cache);
    }
    else
    {
        peaks =
            std::make_shared<waveform::SourcePeaks>(shape, std::move(levels));
    }
    const double prepareMs = elapsed(began);
    std::vector<double> durations;
    uint64_t visited = 0;
    for (auto iteration : measurement)
    {
        (void)iteration;
        for (int pass = 0; pass < 2; ++pass)
        {
            const auto start = Clock::now();
            for (int zoom = 0; zoom < 8; ++zoom)
            {
                const auto window = std::max<int64_t>(1, count >> zoom);
                const auto offset = (count - window) / 3;
                for (int x = 0; x < 1200; ++x)
                {
                    const auto first = offset + window * x / 1200;
                    const auto end = std::min(
                        count,
                        std::max(first + 1, offset + window * (x + 1) / 1200));
                    const auto p =
                        peaks->queryBlocks(zoom % 2, first, end, visited);
                    require(p.min == -.5f && p.max == .75f,
                            "Paged overview mismatch");
                }
            }
            durations.push_back(elapsed(start));
        }
        measurement.SetIterationTime((durations[0] + durations[1]) / 1000.);
    }
    const auto stats = peaks->residency();
    require(cache->stats().peakResidentBytes <= 1024 * 1024,
            "Peak cache exceeded shared budget");
    result["peak_paging"] = {
        {"resident_peak_bytes", stats.residentBytes},
        {"paged_peak_bytes", stats.pagedBytes},
        {"cache_peak_bytes", cache->stats().peakResidentBytes},
        {"peak_read_bytes", stats.bytesRead},
        {"prepare_ms", prepareMs},
        {"first_eight_views_ms", durations[0]},
        {"repeat_eight_views_ms", durations[1]},
        {"visited_peaks", visited}};
    result["milestones_ms"]["background_complete"] =
        prepareMs + durations[0] + durations[1];
    result["validated"] = true;
}
