// Included inside Benchmarks.cpp's anonymous namespace.
void restoredClipboardPaste(benchmark::State &measurement)
{
    const auto root =
        std::filesystem::path(request.at("root").get<std::string>());
    auto snapshot = root / "clipboard";
    const bool supplied = request.contains("clipboard_snapshot");
    const int64_t first = request.at("frames").get<int64_t>() / 4 + 17;
    if (supplied)
    {
        // Explicit diagnostic input: only read this archive. All target state
        // and any autosave output belong to the benchmark's temporary root.
        snapshot = request.at("clipboard_snapshot").get<std::string>();
    }
    else
    {
        auto imported = file::importOwnedAudio(
            request.at("fixture").get<std::string>(), root / "source",
            std::make_shared<storage::DecodedBlockCache>(2 * 1024 * 1024));
        storage::AudioEditTransaction slice(
            *storage::AudioEditRevision::from(imported.audio));
        slice.trim(first, imported.audio->shape().frames / 2);
        ClipboardAudio clip;
        clip.assignRevision(slice.finish());
        require(persistence::saveClipboardSnapshot(snapshot, clip),
                "Could not persist clipboard fixture");
    }
    State state;
    state.paths = std::make_unique<BenchPaths>(root / "target");
    auto began = Clock::now();
    require(persistence::loadClipboardSnapshot(snapshot, state.clipboard),
            "Could not restore clipboard");
    result["clipboard_paste"]["restore_ms"] = elapsed(began);
    const auto original = state.clipboard.getAudioRevision();
    require(bool(original), "Clipboard must contain a revision");
    const auto shape = original->shape();
    std::vector<std::shared_ptr<storage::AudioBlockStore>> stores;
    for (int c = 0; c < shape.channels; ++c)
        original->visitSourceRanges(c, 0, shape.frames, [&](const auto &range)
        {
            if (range.source &&
                std::find(stores.begin(), stores.end(),
                          range.source->blockStore()) == stores.end())
                stores.push_back(range.source->blockStore());
        });
    auto io = [&]
    {
        std::pair<uint64_t, uint64_t> total{};
        for (const auto &store : stores)
        {
            auto bytes = store->ioBytes();
            total.first += bytes.first;
            total.second += bytes.second;
        }
        return total;
    };
    const auto beforeIo = io();
    auto &session = state.getActiveDocumentSession();
    for (auto iteration : measurement)
    {
        (void)iteration;
        performance::resetWork();
        began = Clock::now();
        actions::audio::performPaste(&state);
        finishRevisionCommands(&state);
        result["clipboard_paste"]["paste_ms"] = elapsed(began);
        require(!state.backgroundClipboardConversion &&
                    session.hasReadRevision() &&
                    session.document.getFrameCount() == shape.frames,
                "Paste did not commit by reference");
        const auto pasted = session.getEditRevision();
        state.clipboard.clear();
        auto operation = Clock::now();
        state.undo();
        result["clipboard_paste"]["undo_ms"] = elapsed(operation);
        require(session.document.getFrameCount() == 0 &&
                    !session.revisionHasUnsavedChanges(),
                "Undo did not restore empty saved document");
        operation = Clock::now();
        state.redo();
        result["clipboard_paste"]["redo_ms"] = elapsed(operation);
        require(session.getEditRevision() == pasted,
                "Redo did not restore pasted revision");
        const auto duration = elapsed(began);
        result["milestones_ms"]["command_return"] = duration;
        result["milestones_ms"]["background_complete"] = duration;
        measurement.SetIterationTime(duration / 1000.);
    }
    captureMetrics();
    require(io() == beforeIo, "Paste/history performed sample I/O");
    require(!session.undoStore.isAttached(), "Paste used legacy undo storage");
    result["clipboard_paste"].update({{"frames", shape.frames},
        {"sample_bytes_read", 0}, {"sample_bytes_written", 0}});
    std::array<float, 16384> actual, expected;
    for (int c = 0; c < shape.channels; ++c)
        for (int64_t start = 0; start < shape.frames; start += actual.size())
        {
            const auto count = std::min<int64_t>(actual.size(), shape.frames - start);
            auto out = std::span(actual).first(count);
            session.getAudioReader()->readChannel(c, start, out);
            if (supplied)
                original->readChannel(c, start, std::span(expected).first(count));
            for (int64_t i = 0; i < count; ++i)
                require(std::abs(out[i] - (supplied ? expected[i] :
                    sampleAt(first + start + i, c))) < .000002f,
                    "Restored paste sample mismatch");
        }
    result["validated"] = true;
}
