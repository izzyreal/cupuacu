#include <catch2/catch_test_macros.hpp>
#include "TestPaths.hpp"
#include "persistence/RevisionPersistence.hpp"
#include "persistence/DocumentAutosave.hpp"
#include "actions/audio/SetSampleValue.hpp"
#include "actions/DocumentSessionPersistence.hpp"
#include "actions/Save.hpp"
#include "file/OwnedAudioImport.hpp"
#include "file/RevisionPreservationWriter.hpp"
#include <fstream>
#include <thread>
#include <sndfile.h>

using namespace cupuacu;
namespace
{
    struct Files
    {
        std::filesystem::path root =
            test::makeUniqueTestRoot("revision-persistence");
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
    void fixture(const std::filesystem::path &p)
    {
        SF_INFO info{};
        info.channels = 2;
        info.samplerate = 48000;
        info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_32;
        auto f = sf_open(p.string().c_str(), SFM_WRITE, &info);
        REQUIRE(f);
        std::vector<int> values(4096);
        for (int i = 0; i < 4096; ++i)
        {
            values[i] = 0x40000001 + i * 137;
        }
        REQUIRE(sf_writef_int(f, values.data(), 2048) == 2048);
        REQUIRE(sf_close(f) == 0);
    }
    auto import(State &state, const std::filesystem::path &root)
    {
        auto source = root / "source.wav";
        fixture(source);
        auto imported = file::importOwnedAudio(
            source, root / "working",
            std::make_shared<storage::DecodedBlockCache>(0));
        auto &s = state.getActiveDocumentSession();
        s.document = std::move(imported.metadata.document);
        s.setCurrentFile(source.string(), imported.metadata.exportSettings);
        s.bindReadRevision(storage::AudioEditRevision::from(imported.audio));
        return imported.audio;
    }
    float read(const storage::AudioReader &r, int c, int64_t f)
    {
        float v;
        r.readChannel(c, f, {&v, 1});
        return v;
    }
    template <class F> void until(F done)
    {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!done())
        {
            REQUIRE(std::chrono::steady_clock::now() < deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    auto silence(int64_t frames)
    {
        return storage::AudioEditRevision::silence(
            {frames, 2, 48000, SampleFormat::FLOAT32});
    }
    void bind(DocumentSession &s,
              std::shared_ptr<const storage::AudioEditRevision> r)
    {
        auto shape = r->shape();
        s.document.setExternalAudioShape(shape.format, shape.sampleRate,
                                         shape.channels, shape.frames);
        s.bindReadRevision(std::move(r));
    }
} // namespace
TEST_CASE(
    "Revision recovery restores precise sources, point edits and shared undo "
    "roots",
    "[revision-persistence]")
{
    Files files;
    auto path = files.root / "document";
    std::shared_ptr<const storage::AudioEditRevision> expected;
    {
        State state;
        state.paths.reset();
        auto source = import(state, files.root);
        auto &s = state.getActiveDocumentSession();
        state.addAndDoUndoable(std::make_shared<actions::audio::SetSampleValue>(
            &state, 0, 17, read(*source, 0, 17), -.25f));
        actions::audio::performRevisionCommand(
            &state, actions::audio::RevisionCommand::InsertSilence, 29, 0, 7);
        state.undo(); // Restart must retain a redo stack too.
        expected = s.getEditRevision();
        persistence::RevisionPersistence::save(
            path, *persistence::RevisionPersistence::capture(
                      s, state.getActiveTab()));
        REQUIRE(source->blockStore()->ioBytes().first <=
                storage::AudioBlockBytes);
    }
    // Recovery reads archive files, even after both user and working source
    // disappear.
    std::filesystem::remove(files.root / "source.wav");
    std::filesystem::remove_all(files.root / "working");
    // A new archive path exercises reopening without the writer's registry.
    std::filesystem::rename(path, path.string() + ".moved");
    std::filesystem::rename(path.string() + ".revisions",
                            path.string() + ".moved.revisions");
    path += ".moved";
    State restored;
    restored.paths.reset();
    auto &s = restored.getActiveDocumentSession();
    REQUIRE(persistence::loadDocumentAutosaveSnapshot(path, s));
    REQUIRE(s.hasReadRevision());
    REQUIRE(s.revisionHasUnsavedChanges());
    REQUIRE(persistence::RevisionPersistence::installHistory(&restored, 0));
    REQUIRE(restored.getActiveTab()->undoables.size() == 1);
    REQUIRE(restored.getActiveTab()->redoables.size() == 1);
    REQUIRE(read(*s.getEditRevision(), 0, 17) == -.25f);
    auto settings = *file::defaultExportSettingsForPath(
        files.root / "result.wav", SampleFormat::PCM_S32);
    settings.subtype = SF_FORMAT_PCM_32;
    file::writePreservingRevision(*s.getEditRevision(), s.document.getMarkers(),
                                  s.preservationSource->sourcePath(),
                                  files.root / "result.wav", settings);
    SF_INFO info{};
    auto f =
        sf_open((files.root / "result.wav").string().c_str(), SFM_READ, &info);
    REQUIRE(f);
    std::array<int, 40> bytes;
    REQUIRE(sf_readf_int(f, bytes.data(), 20) == 20);
    REQUIRE(sf_close(f) == 0);
    REQUIRE(bytes[0] == 0x40000001);
    REQUIRE(bytes[34] == -0x20000000);
    restored.undo();
    REQUIRE_FALSE(s.revisionHasUnsavedChanges());
    restored.redo();
    REQUIRE(read(*s.getEditRevision(), 0, 17) == -.25f);
    restored.redo();
    REQUIRE(s.document.getFrameCount() == 2055);
    REQUIRE(read(*s.getEditRevision(), 1, 30) == 0);
    storage::AudioEditRevision::PeakWork work;
    REQUIRE(s.getEditRevision()->prepareWaveform(work));
    REQUIRE(s.getEditRevision()->queryWaveformOverview(0, 0, 2055, work));
}
TEST_CASE("Tiny checkpoints write only new revision nodes",
          "[revision-persistence]")
{
    Files files;
    State state;
    state.paths.reset();
    auto source = import(state, files.root);
    auto &s = state.getActiveDocumentSession();
    auto path = files.root / "document";
    persistence::RevisionPersistence::save(
        path, *persistence::RevisionPersistence::capture(s));
    auto archive = storage::RevisionArchive::open(path);
    const auto initial = archive->stats;
    const auto sourceReads = source->blockStore()->ioBytes().first;
    for (int i = 0; i < 64; ++i)
    {
        auto old = s.getEditRevision();
        storage::AudioEditTransaction edit(*old);
        edit.replaceChannel(0, i * 3, 1, nullptr, 0, 0, .125f);
        REQUIRE(s.commitEditRevision(old, edit.finish(), {}));
        persistence::RevisionPersistence::save(
            path, *persistence::RevisionPersistence::capture(s));
    }
    REQUIRE(archive->stats.sampleBytes == initial.sampleBytes);
    REQUIRE(archive->stats.sourceBytes == initial.sourceBytes);
    REQUIRE(archive->stats.nodes - initial.nodes < 2000);
    REQUIRE(source->blockStore()->ioBytes().first == sourceReads);
    DocumentSession recovered;
    REQUIRE(persistence::loadDocumentAutosaveSnapshot(path, recovered));
    for (int i = 0; i < 64; ++i)
    {
        REQUIRE(read(*recovered.getEditRevision(), 0, i * 3) == .125f);
    }
}
TEST_CASE("Interrupted revision checkpoint leaves the previous manifest usable",
          "[revision-persistence]")
{
    Files files;
    State state;
    state.paths.reset();
    import(state, files.root);
    auto &s = state.getActiveDocumentSession();
    const auto path = files.root / "document";
    persistence::RevisionPersistence::save(
        path, *persistence::RevisionPersistence::capture(s));
    const auto original = read(*s.getEditRevision(), 0, 17);
    auto before = s.getEditRevision();
    storage::AudioEditTransaction edit(*before);
    edit.replaceChannel(0, 17, 1, nullptr, 0, 0, -.5f);
    REQUIRE(s.commitEditRevision(before, edit.finish(), {}));
    auto checkpoint = persistence::RevisionPersistence::capture(s);
    REQUIRE_THROWS(persistence::RevisionPersistence::save(
        path, *checkpoint,
        []
        {
            throw std::runtime_error("interrupted before replacement");
        }));
    DocumentSession old;
    REQUIRE(persistence::loadDocumentAutosaveSnapshot(path, old));
    REQUIRE(read(*old.getEditRevision(), 0, 17) == original);
    persistence::RevisionPersistence::save(path, *checkpoint);
    DocumentSession next;
    REQUIRE(persistence::loadDocumentAutosaveSnapshot(path, next));
    REQUIRE(read(*next.getEditRevision(), 0, 17) == -.5f);
}
TEST_CASE("Removed archives retain live clipboard readers across replacement",
          "[revision-persistence]")
{
    Files files;
    State state;
    state.paths.reset();
    import(state, files.root);
    ClipboardAudio clipboard;
    clipboard.assignRevision(
        state.getActiveDocumentSession().getEditRevision());
    const auto path = files.root / "clipboard";
    REQUIRE(persistence::saveClipboardSnapshot(path, clipboard));
    ClipboardAudio loaded;
    REQUIRE(persistence::loadClipboardSnapshot(path, loaded));
    const auto expected = read(*loaded.getAudioRevision(), 0, 9);
    auto archive = storage::RevisionArchive::open(path);
    auto manifest = archive->readManifest();
    auto generation = std::filesystem::path(path.string() + ".revisions") /
                      manifest.at("generation").get<std::string>();
    archive.reset();
    persistence::removeClipboardSnapshot(path);
    REQUIRE_FALSE(std::filesystem::exists(path));
    clipboard.assignRevision(silence(100));
    REQUIRE(persistence::saveClipboardSnapshot(path, clipboard));
    REQUIRE(read(*loaded.getAudioRevision(), 0, 9) == expected);
    REQUIRE(std::filesystem::exists(generation));
    loaded.clear();
    until(
        [&]
        {
            return !std::filesystem::exists(generation);
        });
    ClipboardAudio replacement;
    REQUIRE(persistence::loadClipboardSnapshot(path, replacement));
    REQUIRE(replacement.getFrameCount() == 100);
}
TEST_CASE("Corrupt or canceled recovery leaves the destination unchanged",
          "[revision-persistence]")
{
    Files files;
    State state;
    state.paths.reset();
    import(state, files.root);
    const auto path = files.root / "document";
    persistence::RevisionPersistence::save(
        path, *persistence::RevisionPersistence::capture(
                  state.getActiveDocumentSession()));
    DocumentSession recovered;
    auto original = silence(10);
    bind(recovered, original);
    REQUIRE_THROWS_AS(persistence::loadDocumentAutosaveSnapshot(path, recovered,
                                                                {},
                                                                []
                                                                {
                                                                    return true;
                                                                }),
                      LongTaskCanceledError);
    REQUIRE(recovered.getEditRevision() == original);
    auto archive = storage::RevisionArchive::open(path);
    auto manifest = archive->readManifest();
    auto index = std::filesystem::path(path.string() + ".revisions") /
                 manifest.at("generation").get<std::string>() / "index.bin";
    std::filesystem::resize_file(index, 4);
    REQUIRE_FALSE(persistence::loadDocumentAutosaveSnapshot(path, recovered));
    REQUIRE(recovered.getEditRevision() == original);
}
TEST_CASE("Malformed manifests can be removed after failed recovery",
          "[revision-persistence]")
{
    Files files;
    const auto path = files.root / "document";
    std::ofstream(path) << "{\"magic\":\"CUPUACU_REVISION\"}";
    DocumentSession recovered;
    auto original = silence(10);
    bind(recovered, original);
    REQUIRE_FALSE(persistence::loadDocumentAutosaveSnapshot(path, recovered));
    REQUIRE(recovered.getEditRevision() == original);
    REQUIRE_NOTHROW(persistence::removeDocumentAutosaveSnapshot(path));
    REQUIRE_FALSE(std::filesystem::exists(path));
}
TEST_CASE("Closing during checkpoint preparation prevents publication",
          "[revision-persistence]")
{
    Files files;
    DocumentSession session;
    bind(session, silence(100));
    const auto path = files.root / "document";
    auto checkpoint = persistence::RevisionPersistence::capture(session);
    REQUIRE_THROWS(persistence::RevisionPersistence::save(
        path, *checkpoint,
        [&]
        {
            persistence::removeDocumentAutosaveSnapshot(path);
        }));
    REQUIRE_FALSE(std::filesystem::exists(path));
}
TEST_CASE(
    "Background revision autosave captures history atomically and retries "
    "newer edits",
    "[revision-persistence]")
{
    Files files;
    State state;
    test::installTestPaths(state, files.root / "state");
    import(state, files.root);
    auto &s = state.getActiveDocumentSession();
    state.addAndDoUndoable(std::make_shared<actions::audio::SetSampleValue>(
        &state, 0, 7, .5f, -.5f));
    REQUIRE(state.backgroundAutosaveJob);
    state.addAndDoUndoable(std::make_shared<actions::audio::SetSampleValue>(
        &state, 0, 8, .5f, -.25f));
    until(
        [&]
        {
            actions::io::processPendingAutosaveWork(&state);
            return !state.backgroundAutosaveJob &&
                   s.autosavedHistoryVersion ==
                       state.getActiveTab()->historyVersion;
        });
    State restored;
    restored.paths.reset();
    REQUIRE(persistence::loadDocumentAutosaveSnapshot(
        s.autosaveSnapshotPath, restored.getActiveDocumentSession()));
    REQUIRE(persistence::RevisionPersistence::installHistory(&restored, 0));
    REQUIRE(restored.getActiveTab()->undoables.size() == 2);
    REQUIRE(read(*restored.getActiveDocumentSession().getEditRevision(), 0,
                 8) == -.25f);
    // Copy adds history without changing audio/marker versions.
    actions::audio::performRevisionCommand(
        &state, actions::audio::RevisionCommand::Copy, 0, 10);
    until(
        [&]
        {
            actions::io::processPendingAutosaveWork(&state);
            return !state.backgroundAutosaveJob &&
                   s.autosavedHistoryVersion ==
                       state.getActiveTab()->historyVersion;
        });
    REQUIRE(persistence::loadDocumentAutosaveSnapshot(
        s.autosaveSnapshotPath, restored.getActiveDocumentSession()));
    REQUIRE(persistence::RevisionPersistence::installHistory(&restored, 0));
    REQUIRE(restored.getActiveTab()->undoables.size() == 3);
}
TEST_CASE(
    "Restart history limit excludes blocks already required by the document",
    "[revision-persistence]")
{
    Files files;
    State state;
    state.paths.reset();
    import(state, files.root);
    auto &s = state.getActiveDocumentSession();
    const auto before = actions::audio::RevisionEditState::capture(s);
    auto shape = before.audio->shape();
    shape.frames = 0;
    auto empty = storage::AudioEditRevision::silence(shape);
    REQUIRE(s.commitEditRevision(before.audio, empty, {}));
    s.markRevisionSaved(empty, {});
    s.preservationSource.reset();
    const auto after = actions::audio::RevisionEditState::capture(s);
    state.addUndoable(std::make_shared<actions::audio::RevisionEdit>(
        &state, 0, "Erase", before, after));
    const auto cp =
        persistence::RevisionPersistence::capture(s, state.getActiveTab());
    const auto path = files.root / "document";
    persistence::RevisionPersistence::save(path, *cp, {}, 1);
    auto archive = storage::RevisionArchive::open(path);
    REQUIRE(archive->stats.sampleBytes == 0);
    State restored;
    restored.paths.reset();
    REQUIRE(persistence::loadDocumentAutosaveSnapshot(
        path, restored.getActiveDocumentSession()));
    REQUIRE(persistence::RevisionPersistence::installHistory(&restored, 0));
    REQUIRE(restored.getActiveTab()->undoables.empty());
    REQUIRE(restored.startupRestore.historyRestoreFailed);
    persistence::RevisionPersistence::save(
        path, *cp, {}, 0); // Existing zero means unlimited policy.
    REQUIRE(persistence::loadDocumentAutosaveSnapshot(
        path, restored.getActiveDocumentSession()));
    REQUIRE(persistence::RevisionPersistence::installHistory(&restored, 0));
    REQUIRE(restored.getActiveTab()->undoables.size() == 1);
    restored.undo();
    REQUIRE(restored.getActiveDocumentSession().document.getFrameCount() ==
            2048);
}
TEST_CASE("A saved revision remains clean while retaining restart history",
          "[revision-persistence]")
{
    Files files;
    State state;
    test::installTestPaths(state, files.root / "state");
    import(state, files.root);
    auto &s = state.getActiveDocumentSession();
    state.addAndDoUndoable(std::make_shared<actions::audio::SetSampleValue>(
        &state, 0, 17, .5f, -.25f));
    until(
        [&]
        {
            actions::io::processPendingAutosaveWork(&state);
            return !state.backgroundAutosaveJob;
        });
    const auto path = s.autosaveSnapshotPath;
    actions::detail::finalizeSavedDocument(&state, files.root / "saved.wav",
                                           *s.currentFileExportSettings, true);
    REQUIRE(s.autosaveSnapshotPath == path);
    REQUIRE_FALSE(actions::documentSessionHasUnsavedChanges(s));
    until(
        [&]
        {
            actions::io::processPendingAutosaveWork(&state);
            return !state.backgroundAutosaveJob &&
                   s.autosavedHistoryVersion ==
                       state.getActiveTab()->historyVersion;
        });
    State restored;
    restored.paths.reset();
    REQUIRE(persistence::loadDocumentAutosaveSnapshot(
        path, restored.getActiveDocumentSession()));
    REQUIRE(persistence::RevisionPersistence::installHistory(&restored, 0));
    REQUIRE_FALSE(actions::documentSessionHasUnsavedChanges(
        restored.getActiveDocumentSession()));
    restored.undo();
    REQUIRE(restored.getActiveDocumentSession().revisionHasUnsavedChanges());
    restored.redo();
    REQUIRE_FALSE(
        restored.getActiveDocumentSession().revisionHasUnsavedChanges());
}
TEST_CASE("Autosave failures are reported and retries are throttled",
          "[revision-persistence]")
{
    Files files;
    State state;
    test::installTestPaths(state, files.root / "state");
    import(state, files.root);
    int errors = 0;
    state.errorReporter = [&](auto, auto)
    {
        ++errors;
    };
    std::ofstream(files.root / "blocked").put('x');
    auto &s = state.getActiveDocumentSession();
    s.autosaveSnapshotPath = files.root / "blocked" / "checkpoint";
    state.addAndDoUndoable(std::make_shared<actions::audio::SetSampleValue>(
        &state, 0, 1, .5f, -.5f));
    until(
        [&]
        {
            actions::io::processPendingAutosaveWork(&state);
            return !state.backgroundAutosaveJob;
        });
    REQUIRE(errors == 1);
    REQUIRE_FALSE(s.lastAutosaveError.empty());
    REQUIRE(s.revisionHasUnsavedChanges());
    actions::io::processPendingAutosaveWork(&state);
    REQUIRE_FALSE(state.backgroundAutosaveJob);
    REQUIRE(errors == 1);
}
TEST_CASE(
    "Replacing the clipboard reclaims obsolete stores after readers release",
    "[revision-persistence]")
{
    Files files;
    State state;
    state.paths.reset();
    import(state, files.root);
    ClipboardAudio original;
    original.assignRevision(state.getActiveDocumentSession().getEditRevision());
    const auto path = files.root / "clipboard";
    REQUIRE(persistence::saveClipboardSnapshot(path, original));
    ClipboardAudio reader;
    REQUIRE(persistence::loadClipboardSnapshot(path, reader));
    std::filesystem::path oldStore;
    reader.getAudioRevision()->visitSourceRanges(
        0, 0, 1,
        [&](const auto &range)
        {
            oldStore = range.source->blockStore()->path();
        });
    ClipboardAudio replacement;
    replacement.assignRevision(silence(100));
    REQUIRE(persistence::saveClipboardSnapshot(path, replacement));
    REQUIRE(std::filesystem::exists(oldStore));
    REQUIRE(read(*reader.getAudioRevision(), 0, 0) > 0);
    reader.clear();
    until(
        [&]
        {
            return !std::filesystem::exists(oldStore);
        });
    // An original working revision can be copied again after its old archive
    // records have been reclaimed; cached IDs must not name deleted blocks.
    REQUIRE(persistence::saveClipboardSnapshot(path, original));
    REQUIRE(persistence::loadClipboardSnapshot(path, reader));
    REQUIRE(read(*reader.getAudioRevision(), 0, 0) > 0);
}
TEST_CASE("Damaged waveform pages rebuild without losing committed audio",
          "[revision-persistence]")
{
    Files files;
    State state;
    state.paths.reset();
    import(state, files.root);
    const auto path = files.root / "document";
    persistence::RevisionPersistence::save(
        path, *persistence::RevisionPersistence::capture(
                  state.getActiveDocumentSession()));
    auto archive = storage::RevisionArchive::open(path);
    auto manifest = archive->readManifest();
    const auto index = std::filesystem::path(path.string() + ".revisions") /
                       manifest.at("generation").get<std::string>() /
                       "index.bin";
    auto record = [&](uint64_t id)
    {
        std::ifstream in(index, std::ios::binary);
        in.seekg(id);
        uint32_t length = 0;
        for (int i = 0; i < 4; ++i)
        {
            length |= uint32_t(in.get()) << (i * 8);
        }
        in.seekg(4, std::ios::cur);
        std::vector<uint8_t> bytes(length);
        in.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
        REQUIRE(in.good());
        return nlohmann::json::from_cbor(bytes);
    };
    const auto root = record(manifest.at("current").at("root"));
    const auto leaf = record(root.at("channels").at(0));
    const auto source = record(leaf.at("source"));
    const auto peak =
        source.at("peaks").at(0).at(0).at("first").get<uint64_t>();
    std::fstream damaged(index,
                         std::ios::binary | std::ios::in | std::ios::out);
    damaged.seekp(peak + 8);
    damaged.put('\0');
    damaged.close();
    DocumentSession restored;
    REQUIRE(persistence::loadDocumentAutosaveSnapshot(path, restored));
    REQUIRE(read(*restored.getEditRevision(), 0, 0) > .49f);
    storage::AudioEditRevision::PeakWork work;
    REQUIRE(restored.getEditRevision()->prepareWaveform(work));
    REQUIRE(
        restored.getEditRevision()->queryWaveformOverview(0, 0, 2048, work));
}

TEST_CASE("Paged peak summaries survive archive round trips", "[paged-peaks]")
{
    Files files;
    constexpr int64_t frames = 128 * 32771;
    storage::AudioShape shape{frames, 1, 48000, SampleFormat::FLOAT32};
    auto cache =
        std::make_shared<storage::DecodedBlockCache>(storage::AudioBlockBytes);
    auto store =
        std::make_shared<storage::AudioBlockStore>(files.root / "working");
    storage::AudioRevisionBuilder builder(shape, store, cache);
    std::vector<float> samples(65536, .375f);
    for (int64_t first = 0; first < frames; first += samples.size())
    {
        builder.appendInterleaved(std::span<const float>(samples).first(
            std::min<int64_t>(samples.size(), frames - first)));
    }
    gui::PeakLevel base;
    base.resize(32771);
    for (std::size_t i = 0; i < base.size(); ++i)
    {
        base.set(i, {.375f, .375f});
    }
    auto source = builder.finish(
        {}, waveform::SourcePeaks::createPaged(shape, {{base}}, cache));
    auto archive = storage::RevisionArchive::open(files.root / "peaks");
    const auto id = archive->saveSource(source);
    archive->commit({{"source", id}});
    archive.reset();
    archive = storage::RevisionArchive::open(files.root / "peaks");
    archive->readManifest();
    auto loaded = archive->loadSource(id);
    REQUIRE(loaded->sourcePeaks()->residency().pagedBytes > 0);
    uint64_t visited = 0;
    const auto p = loaded->sourcePeaks()->queryBlocks(0, 16383, 32770, visited);
    CHECK(p.min == .375f);
    CHECK(p.max == .375f);
    CHECK(read(*loaded, 0, frames - 1) == .375f);
}
