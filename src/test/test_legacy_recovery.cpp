#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "TestPaths.hpp"
#include "persistence/DocumentAutosave.hpp"
#include "persistence/RevisionPersistence.hpp"
#include "persistence/SessionStatePersistence.hpp"
#include "undo/UndoManifestPersistence.hpp"
#include "LongTask.hpp"
#include "actions/audio/EditCommands.hpp"
#include "TestRevisionCommands.hpp"
#include <fstream>

using namespace cupuacu;
namespace
{
    using Json = nlohmann::json;
    struct Files
    {
        std::filesystem::path root =
            test::makeUniqueTestRoot("legacy-recovery");
        Files()
        {
            std::filesystem::create_directories(root);
        }
        ~Files()
        {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }
    };
    void initialize(DocumentSession &s,
                    const std::vector<std::vector<float>> &samples)
    {
        s.document.initialize(SampleFormat::FLOAT32, 48000, samples.size(),
                              samples[0].size());
        for (std::size_t c = 0; c < samples.size(); ++c)
        {
            s.document.writeChannelFloatBlock(c, 0, samples[c].data(),
                                              samples[c].size(), false);
        }
    }
    auto read(const DocumentSession &s)
    {
        std::vector<std::vector<float>> result(
            s.document.getChannelCount(),
            std::vector<float>(s.document.getFrameCount()));
        for (int c = 0; c < s.document.getChannelCount(); ++c)
        {
            s.getAudioReader()->readChannel(c, 0, result[c]);
        }
        return result;
    }
    Json manifest(const Json &entry, bool redo)
    {
        return {{"version", 1},
                {"entries", redo ? Json::array() : Json::array({entry})},
                {"redoEntries", redo ? Json::array({entry}) : Json::array()}};
    }
} // namespace

TEST_CASE("Legacy operation payloads migrate to reference history",
          "[legacy-recovery]")
{
    const auto kind =
        GENERATE("cut", "delete", "paste", "trim", "make-silent", "reverse",
                 "amplify-fade", "amplify-envelope", "dynamics",
                 "remove-silence-compact", "remove-silence-duration",
                 "set-sample-value", "copy", "record-edit");
    const bool redo = GENERATE(false, true);
    CAPTURE(kind, redo);
    Files files;
    undo::UndoStore store;
    store.attach(files.root / "undo");
    std::vector<std::vector<float>> before{{0, 1, 2, 3, 4, 5}}, after = before;
    auto segment = [&](std::vector<float> samples)
    {
        DocumentSession s;
        initialize(s, {samples});
        return store.writeSegment(s.document.captureSegment(0, samples.size()))
            .path.string();
    };
    auto matrix = [&](std::vector<float> samples)
    {
        return store.writeSampleMatrix({samples}).path.string();
    };
    Json entry{{"kind", kind},           {"startFrame", 1},
               {"frameCount", 2},        {"oldSelectionStart", 1.0},
               {"oldSelectionEnd", 3.0}, {"oldCursorPos", 2},
               {"hadOldSelection", true}};
    const std::string name(kind);
    if (name == "cut" || name == "delete")
    {
        entry["removedHandle"] = segment({1, 2});
        after = {{0, 3, 4, 5}};
    }
    else if (name == "paste")
    {
        entry.update({{"endFrame", 3},
                      {"insertedFrameCount", 3},
                      {"overwrittenFrameCount", 2},
                      {"insertedHandle", segment({8, 9, 10})},
                      {"overwrittenHandle", segment({1, 2})}});
        after = {{0, 8, 9, 10, 3, 4, 5}};
    }
    else if (name == "trim")
    {
        entry.update(
            {{"beforeCount", 1},
             {"middleCount", 3},
             {"afterCount", 2},
             {"beforeHandle", segment({0})},
             {"afterHandle", segment({4, 5})},
             {"preTrimView", {{"samplesPerPixel", 2.0}, {"sampleOffset", 1}}},
             {"postTrimView", {{"samplesPerPixel", 1.0}, {"sampleOffset", 0}}},
             {"hasPostTrimView", true}});
        after = {{1, 2, 3}};
    }
    else if (name == "make-silent")
    {
        entry.update(
            {{"targetChannels", {0}}, {"originalHandle", segment({1, 2})}});
        after = {{0, 0, 0, 3, 4, 5}};
    }
    else if (name == "set-sample-value")
    {
        entry.update({{"channel", 0},
                      {"sampleIndex", 1},
                      {"oldValue", 1.0},
                      {"newValue", -0.25}});
        after[0][1] = -0.25;
    }
    else if (name == "copy")
    {
        entry.update({{"numFrames", 2}, {"oldSel1", 1.0}, {"oldSel2", 3.0}});
    }
    else if (name == "record-edit")
    {
        entry.update({{"endFrame", 8},
                      {"oldFrameCount", 6},
                      {"oldChannelCount", 1},
                      {"targetChannelCount", 1},
                      {"overwrittenOldSamplesHandle", matrix({1, 2, 3, 4, 5})},
                      {"recordedSamplesHandle", matrix({9, 8, 7, 6, 5, 4, 3})},
                      {"hadNewSelection", false},
                      {"newSelectionStart", 0.0},
                      {"newSelectionEnd", 0.0},
                      {"oldCursor", 1},
                      {"newCursor", 8}});
        after = {{0, 9, 8, 7, 6, 5, 4, 3}};
    }
    else if (name == "remove-silence-duration")
    {
        entry.update(
            {{"runs", Json::array({{{"startFrame", 1}, {"frameCount", 1}},
                                   {{"startFrame", 4}, {"frameCount", 1}}})},
             {"removedSamplesHandle",
              store.writeSampleCube({{{1}}, {{4}}}).path.string()},
             {"relevantStart", 0},
             {"originalRelevantLength", 6},
             {"hadSelection", true},
             {"originalCursor", 2}});
        after = {{0, 2, 3, 5}};
    }
    else
    {
        entry.update({{"targetChannels", {0}},
                      {"oldSamplesHandle", matrix({1, 2})},
                      {"newSamplesHandle", matrix({8, 9})}});
        after = {{0, 8, 9, 3, 4, 5}};
    }
    DocumentSession legacy;
    initialize(legacy, redo ? before : after);
    const auto path = files.root / "snapshot";
    REQUIRE(persistence::saveDocumentAutosaveSnapshot(path, legacy));
    std::ofstream(store.root() / "manifest.json") << manifest(entry, redo);
    persistence::PersistedOpenDocumentState info;
    info.undoStorePath = store.root().string();
    State recovered;
    recovered.paths.reset();
    auto &session = recovered.getActiveDocumentSession();
    REQUIRE(persistence::loadDocumentAutosaveSnapshot(path, session, {}, {},
                                                      &info));
    REQUIRE(session.hasReadRevision());
    REQUIRE(session.recoveredRevisionCheckpoint->historyWarning.empty());
    REQUIRE(persistence::RevisionPersistence::installHistory(&recovered, 0));
    REQUIRE(read(session) == (redo ? before : after));
    if (redo)
    {
        recovered.redo();
    }
    else
    {
        recovered.undo();
    }
    REQUIRE(read(session) == (redo ? after : before));
    if (redo)
    {
        recovered.undo();
    }
    else
    {
        recovered.redo();
    }
    REQUIRE(read(session) == (redo ? before : after));
    const auto durable = files.root / "converted";
    persistence::RevisionPersistence::save(
        durable, *persistence::RevisionPersistence::capture(
                     session, recovered.getActiveTab()));
    REQUIRE(storage::RevisionArchive::recognizes(
        path)); // conversion committed atomically
    State restarted;
    restarted.paths.reset();
    persistence::RevisionPersistence::load(
        durable, restarted.getActiveDocumentSession());
    REQUIRE(persistence::RevisionPersistence::installHistory(&restarted, 0));
    std::filesystem::remove_all(store.root());
    if (redo)
    {
        restarted.redo();
    }
    else
    {
        restarted.undo();
    }
    REQUIRE(read(restarted.getActiveDocumentSession()) ==
            (redo ? after : before));
}

TEST_CASE(
    "Legacy recovery streams block boundaries and keeps failed targets intact",
    "[legacy-recovery]")
{
    Files files;
    DocumentSession legacy;
    std::vector<std::vector<float>> samples(2,
                                            std::vector<float>(65536 * 2 + 17));
    for (int c = 0; c < 2; ++c)
    {
        for (std::size_t i = 0; i < samples[c].size(); ++i)
        {
            samples[c][i] = float(int(i % 31) - 15 + c) / 16;
        }
    }
    initialize(legacy, samples);
    const auto path = files.root / "snapshot";
    REQUIRE(persistence::saveDocumentAutosaveSnapshot(path, legacy));
    DocumentSession target;
    initialize(target, {{0.5}});
    SECTION("complete")
    {
        REQUIRE(persistence::loadDocumentAutosaveSnapshot(path, target));
        REQUIRE(target.hasReadRevision());
        REQUIRE(read(target) == samples);
        storage::AudioEditRevision::PeakWork work;
        REQUIRE(target.getEditRevision()->prepareWaveform(work));
        for (int c = 0; c < 2; ++c)
        {
            const auto peak = target.getEditRevision()->queryWaveformOverview(
                c, 0, samples[c].size(), work);
            REQUIRE(peak);
            REQUIRE(peak->min ==
                    *std::min_element(samples[c].begin(), samples[c].end()));
            REQUIRE(peak->max ==
                    *std::max_element(samples[c].begin(), samples[c].end()));
        }
    }
    SECTION("cancel")
    {
        int checks = 0;
        REQUIRE_THROWS_AS(
            persistence::loadDocumentAutosaveSnapshot(path, target, {},
                                                      [&]
                                                      {
                                                          return ++checks == 3;
                                                      }),
            LongTaskCanceledError);
        REQUIRE(read(target) == std::vector<std::vector<float>>{{0.5}});
    }
    SECTION("truncated")
    {
        std::filesystem::resize_file(path,
                                     std::filesystem::file_size(path) - 1);
        REQUIRE_FALSE(persistence::loadDocumentAutosaveSnapshot(path, target));
        REQUIRE(read(target) == std::vector<std::vector<float>>{{0.5}});
    }
}

TEST_CASE("Unsupported legacy history retains recovery inputs",
          "[legacy-recovery]")
{
    Files files;
    DocumentSession legacy;
    initialize(legacy, {{0.25, -0.5}});
    const auto path = files.root / "snapshot";
    REQUIRE(persistence::saveDocumentAutosaveSnapshot(path, legacy));
    const auto originalBytes = std::filesystem::file_size(path);
    undo::UndoStore store;
    store.attach(files.root / "undo");
    const auto payload = store.writeSampleMatrix({{0.75}}).path;
    std::ofstream(store.root() / "manifest.json")
        << manifest({{"kind", "unknown-future-command"}}, false);
    persistence::PersistedOpenDocumentState info;
    info.undoStorePath = store.root().string();
    DocumentSession recovered;
    REQUIRE(persistence::loadDocumentAutosaveSnapshot(path, recovered, {}, {},
                                                      &info));
    REQUIRE(recovered.hasReadRevision());
    REQUIRE(read(recovered) == read(legacy));
    REQUIRE_FALSE(
        recovered.recoveredRevisionCheckpoint->historyWarning.empty());
    REQUIRE(recovered.recoveredRevisionCheckpoint->undo.empty());
    const auto retained = files.root / "legacy-recovery-retained";
    REQUIRE(std::filesystem::is_directory(retained));
    const auto backup = std::filesystem::directory_iterator(retained)->path();
    REQUIRE(std::filesystem::file_size(backup / "snapshot.cupuacu-autosave") ==
            originalBytes);
    REQUIRE(std::filesystem::file_size(backup / "undo" / payload.filename()) ==
            std::filesystem::file_size(payload));
    REQUIRE(storage::RevisionArchive::recognizes(path));
}

TEST_CASE(
    "Canceling legacy history conversion keeps the destination and original "
    "files",
    "[legacy-recovery]")
{
    Files files;
    DocumentSession legacy;
    initialize(legacy, {{0.25}});
    const auto path = files.root / "snapshot";
    REQUIRE(persistence::saveDocumentAutosaveSnapshot(path, legacy));
    undo::UndoStore store;
    store.attach(files.root / "undo");
    const auto payload =
        store.writeSampleMatrix({std::vector<float>(65536, 0.5)}).path;
    Json entry{{"kind", "reverse"},
               {"startFrame", 0},
               {"frameCount", 1},
               {"targetChannels", {0}},
               {"oldSamplesHandle", payload.string()},
               {"newSamplesHandle", payload.string()}};
    std::ofstream(store.root() / "manifest.json") << manifest(entry, false);
    persistence::PersistedOpenDocumentState info;
    info.undoStorePath = store.root().string();
    DocumentSession target;
    initialize(target, {{-0.75}});
    int checks = 0;
    REQUIRE_THROWS_AS(persistence::loadDocumentAutosaveSnapshot(
                          path, target, {},
                          [&]
                          {
                              return ++checks == 6;
                          },
                          &info),
                      LongTaskCanceledError);
    REQUIRE(read(target) == std::vector<std::vector<float>>{{-0.75}});
    REQUIRE(std::filesystem::exists(payload));
    REQUIRE_FALSE(storage::RevisionArchive::recognizes(path));
}

TEST_CASE(
    "Failed legacy archive publication leaves the original snapshot "
    "recoverable",
    "[legacy-recovery]")
{
    Files files;
    DocumentSession legacy;
    initialize(legacy, {{0.125, -0.5}});
    const auto path = files.root / "snapshot";
    REQUIRE(persistence::saveDocumentAutosaveSnapshot(path, legacy));
    DocumentSession target;
    initialize(target, {{0.75}});
    SECTION("cancel after streaming")
    {
        bool cancel = false;
        REQUIRE_THROWS_AS(persistence::loadDocumentAutosaveSnapshot(
                              path, target,
                              [&](std::optional<double> progress)
                              {
                                  if (!progress)
                                  {
                                      cancel = true;
                                  }
                              },
                              [&]
                              {
                                  return cancel;
                              }),
                          LongTaskCanceledError);
    }
    SECTION("archive directory cannot be written")
    {
        const auto blocked =
            std::filesystem::path(path.string() + ".revisions");
        std::ofstream(blocked) << "blocked";
        REQUIRE_FALSE(persistence::loadDocumentAutosaveSnapshot(path, target));
        std::filesystem::remove(blocked);
    }
    REQUIRE_FALSE(storage::RevisionArchive::recognizes(path));
    REQUIRE(read(target) == std::vector<std::vector<float>>{{0.75}});
    REQUIRE(persistence::loadDocumentAutosaveSnapshot(path, target));
    REQUIRE(storage::RevisionArchive::recognizes(path));
    REQUIRE(read(target) == read(legacy));
}

TEST_CASE("Legacy recording into an unconfigured tab keeps compatible undo",
          "[legacy-recovery]")
{
    Files files;
    DocumentSession legacy;
    initialize(legacy, {{0.25, -0.5}});
    const auto path = files.root / "snapshot";
    REQUIRE(persistence::saveDocumentAutosaveSnapshot(path, legacy));
    undo::UndoStore store;
    store.attach(files.root / "undo");
    Json entry{{"kind", "record-edit"},
               {"startFrame", 0},
               {"endFrame", 2},
               {"oldFrameCount", 0},
               {"oldChannelCount", 0},
               {"targetChannelCount", 1},
               {"oldSampleRate", 0},
               {"newSampleRate", 48000},
               {"oldFormat", int(SampleFormat::Unknown)},
               {"newFormat", int(SampleFormat::FLOAT32)},
               {"overwrittenOldSamplesHandle",
                store.writeSampleMatrix({}).path.string()},
               {"recordedSamplesHandle",
                store.writeSampleMatrix({{0.25, -0.5}}).path.string()}};
    std::ofstream(store.root() / "manifest.json") << manifest(entry, false);
    persistence::PersistedOpenDocumentState info;
    info.undoStorePath = store.root().string();
    State restored;
    restored.paths.reset();
    auto &session = restored.getActiveDocumentSession();
    REQUIRE(persistence::loadDocumentAutosaveSnapshot(path, session, {}, {},
                                                      &info));
    REQUIRE(session.hasReadRevision());
    REQUIRE(storage::RevisionArchive::recognizes(path));
    REQUIRE(persistence::RevisionPersistence::installHistory(&restored, 0));
    restored.undo();
    REQUIRE(session.document.getChannelCount() == 0);
    REQUIRE(session.document.getFrameCount() == 0);
    REQUIRE(session.hasReadRevision());
    REQUIRE(session.document.getSampleRate() == 0);
    REQUIRE(session.document.getSampleFormat() == SampleFormat::Unknown);
    auto checkpoint =
        persistence::RevisionPersistence::capture(session, &restored.tabs[0]);
    const auto emptyPath = files.root / "empty-revision";
    persistence::RevisionPersistence::save(emptyPath, *checkpoint);
    DocumentSession reopened;
    REQUIRE(persistence::loadDocumentAutosaveSnapshot(emptyPath, reopened));
    REQUIRE(reopened.hasReadRevision());
    REQUIRE(reopened.document.getChannelCount() == 0);
    restored.redo();
    REQUIRE(read(session) == read(legacy));
    restored.undo();
    restored.clipboard.assignRevision(checkpoint->redo.back().after.audio);
    actions::audio::performPaste(&restored);
    test::finishRevisionCommands(&restored);
    REQUIRE(read(session) == read(legacy));
    restored.undo();
    REQUIRE(session.document.getChannelCount() == 0);
}

TEST_CASE(
    "All legacy segment versions stream across channel and block boundaries",
    "[legacy-recovery][legacy-segments]")
{
    const auto version = GENERATE(1, 2, 3);
    CAPTURE(version);
    Files files;
    constexpr int64_t count = 65537;
    std::vector<std::vector<float>> before(2, std::vector<float>(count + 2));
    for (int c = 0; c < 2; ++c)
    {
        for (int64_t i = 0; i < count + 2; ++i)
        {
            before[c][i] = float((i % 31) - 15 + c) / 16;
        }
    }
    const std::vector<std::vector<float>> after{
        {before[0].front(), before[0].back()},
        {before[1].front(), before[1].back()}};
    DocumentSession legacy;
    initialize(legacy, after);
    const auto path = files.root / "snapshot";
    REQUIRE(persistence::saveDocumentAutosaveSnapshot(path, legacy));
    const auto undoRoot = files.root / "undo";
    std::filesystem::create_directory(undoRoot);
    const auto payload = undoRoot / "segment.bin";
    {
        std::ofstream out(payload, std::ios::binary);
        const char magic[] = "CUPUACU_UNDO_SEGMENT";
        out.write(magic, sizeof(magic));
        auto integer = [&](uint64_t value, int width = 8)
        {
            for (int n = 0; n < width; ++n)
            {
                out.put(char(value >> (n * 8)));
            }
        };
        integer(version, 4);
        integer(int(SampleFormat::FLOAT32), 4);
        integer(48000, 4);
        integer(2);
        integer(count);
        for (int c = 0; c < 2; ++c)
        {
            for (int64_t i = 1; i <= count; ++i)
            {
                integer(std::bit_cast<uint32_t>(before[c][i]), 4);
                if (version == 1)
                {
                    integer(0);
                    integer(UINT64_MAX);
                }
            }
            if (version == 3)
            {
                for (int64_t i = 0; i < count; ++i)
                {
                    out.put(1);
                }
            }
            if (version >= 2)
            {
                integer(1);
                integer(0);
                integer(count);
                integer(0);
                integer(UINT64_MAX);
            }
        }
        REQUIRE(out.good());
    }
    std::ofstream(undoRoot / "manifest.json")
        << manifest({{"kind", "cut"},
                     {"startFrame", 1},
                     {"frameCount", count},
                     {"removedHandle", payload.string()}},
                    false);
    persistence::PersistedOpenDocumentState info;
    info.undoStorePath = undoRoot.string();
    State state;
    state.paths.reset();
    auto &session = state.getActiveDocumentSession();
    REQUIRE(persistence::loadDocumentAutosaveSnapshot(path, session, {}, {},
                                                      &info));
    REQUIRE(session.hasReadRevision());
    REQUIRE(session.recoveredRevisionCheckpoint->historyWarning.empty());
    REQUIRE(persistence::RevisionPersistence::installHistory(&state, 0));
    state.undo();
    REQUIRE(read(session) == before);
    state.redo();
    REQUIRE(read(session) == after);
}

TEST_CASE(
    "Legacy recording channel expansion preserves both history directions",
    "[legacy-recovery]")
{
    const bool restoredRedo = GENERATE(false, true);
    Files files;
    DocumentSession legacy;
    const std::vector<std::vector<float>> before{{.125f, -.25f, .375f, -.5f}};
    const std::vector<std::vector<float>> after{{.125f, .75f, -.75f, -.5f},
                                                {0.f, .5f, -.5f, 0.f}};
    initialize(legacy, restoredRedo ? before : after);
    const auto path = files.root / "expanded.snapshot";
    REQUIRE(persistence::saveDocumentAutosaveSnapshot(path, legacy));
    undo::UndoStore store;
    store.attach(files.root / "undo");
    Json entry{
        {"kind", "record-edit"},
        {"startFrame", 1},
        {"endFrame", 3},
        {"oldFrameCount", 4},
        {"oldChannelCount", 1},
        {"targetChannelCount", 2},
        {"oldSampleRate", 48000},
        {"newSampleRate", 48000},
        {"oldFormat", int(SampleFormat::FLOAT32)},
        {"newFormat", int(SampleFormat::FLOAT32)},
        {"overwrittenOldSamplesHandle",
         store.writeSampleMatrix({{-.25f, .375f}}).path.string()},
        {"recordedSamplesHandle",
         store.writeSampleMatrix({{.75f, -.75f}, {.5f, -.5f}}).path.string()}};
    std::ofstream(store.root() / "manifest.json")
        << manifest(entry, restoredRedo);
    persistence::PersistedOpenDocumentState info;
    info.undoStorePath = store.root().string();
    State state;
    state.paths.reset();
    auto &session = state.getActiveDocumentSession();
    REQUIRE(persistence::loadDocumentAutosaveSnapshot(path, session, {}, {},
                                                      &info));
    REQUIRE(session.hasReadRevision());
    REQUIRE(persistence::RevisionPersistence::installHistory(&state, 0));
    if (restoredRedo)
    {
        state.redo();
    }
    REQUIRE(read(session) == after);
    state.undo();
    REQUIRE(read(session) == before);
    state.redo();
    REQUIRE(read(session) == after);
}
