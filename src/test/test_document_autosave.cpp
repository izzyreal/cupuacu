#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestPaths.hpp"
#include "actions/DocumentRestore.hpp"
#include "actions/DocumentSessionPersistence.hpp"
#include "actions/DocumentLifecycle.hpp"
#include "actions/DocumentTabs.hpp"
#include "actions/DocumentUi.hpp"
#include "actions/audio/EditCommands.hpp"
#include "actions/audio/SetSampleValue.hpp"
#include "actions/markers/EditCommands.hpp"
#include "actions/io/BackgroundSave.hpp"
#include "actions/Undoable.hpp"
#include "persistence/DocumentAutosave.hpp"
#include "persistence/SessionStatePersistence.hpp"
#include "undo/UndoManifestPersistence.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>
#include <memory>
#include <vector>

namespace
{
    void initializeMonoDocument(cupuacu::State &state,
                                const std::vector<float> &samples)
    {
        auto &document = state.getActiveDocumentSession().document;
        document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1,
                            static_cast<int64_t>(samples.size()));
        for (size_t i = 0; i < samples.size(); ++i)
        {
            document.setSample(0, static_cast<int64_t>(i), samples[i], false);
        }
    }

    template <typename AudioLike>
    std::vector<float> readMonoSamples(const AudioLike &document)
    {
        std::vector<float> result(
            static_cast<std::size_t>(document.getFrameCount()));
        for (int64_t i = 0; i < document.getFrameCount(); ++i)
        {
            result[static_cast<std::size_t>(i)] = document.getSample(0, i);
        }
        return result;
    }

    void requireBuildStatesEqual(
        const cupuacu::gui::WaveformCache::BuildState &expected,
        const cupuacu::gui::WaveformCache::BuildState &actual)
    {
        REQUIRE(actual.numSamples == expected.numSamples);
        REQUIRE(actual.dirtyFromBlock == expected.dirtyFromBlock);
        REQUIRE(actual.dirtyToBlock == expected.dirtyToBlock);
        REQUIRE(actual.levels.size() == expected.levels.size());
        for (std::size_t levelIndex = 0; levelIndex < expected.levels.size();
             ++levelIndex)
        {
            const auto &expectedLevel = expected.levels[levelIndex];
            const auto &actualLevel = actual.levels[levelIndex];
            REQUIRE(actualLevel.size() == expectedLevel.size());
            for (std::size_t peakIndex = 0; peakIndex < expectedLevel.size();
                 ++peakIndex)
            {
                REQUIRE(actualLevel[peakIndex].min ==
                        Catch::Approx(expectedLevel[peakIndex].min));
                REQUIRE(actualLevel[peakIndex].max ==
                        Catch::Approx(expectedLevel[peakIndex].max));
            }
        }
    }

    class SetSampleUndoable : public cupuacu::actions::Undoable
    {
    public:
        SetSampleUndoable(cupuacu::State *stateToUse, const int64_t frameToUse,
                          const float valueToUse)
            : cupuacu::actions::Undoable(stateToUse), frame(frameToUse),
              value(valueToUse)
        {
            previousValue = state->getActiveDocumentSession().document.getSample(
                0, frame);
        }

        void redo() override
        {
            state->getActiveDocumentSession().document.setSample(0, frame,
                                                                 value);
        }

        void undo() override
        {
            state->getActiveDocumentSession().document.setSample(0, frame,
                                                                 previousValue);
        }

        std::string getRedoDescription() override
        {
            return "Set sample";
        }

        std::string getUndoDescription() override
        {
            return "Unset sample";
        }

        [[nodiscard]] cupuacu::file::OverwritePreservationMutation
        overwritePreservationMutation() const override
        {
            return cupuacu::file::OverwritePreservationMutationHelper::compatible();
        }

    private:
        int64_t frame = 0;
        float value = 0.0f;
        float previousValue = 0.0f;
    };

    void drainPendingAutosave(cupuacu::State &state)
    {
        for (int i = 0; i < 100000; ++i)
        {
            cupuacu::actions::io::processPendingAutosaveWork(&state);
            if (!state.backgroundAutosaveJob)
            {
                return;
            }
        }
        FAIL("Autosave job did not complete");
    }
} // namespace

TEST_CASE("Document autosave snapshots preserve untitled audio and markers",
          "[autosave]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("document-autosave");
    cupuacu::test::StateWithTestPaths state{root};
    initializeMonoDocument(state, {0.25f, -0.5f, 0.75f});
    state.getActiveDocumentSession().document.addMarker(2, "point");
    state.getActiveDocumentSession().rebuildWaveformCacheSynchronously();
    const auto cacheState =
        state.getActiveDocumentSession().getWaveformCache(0).snapshotBuildState();

    const auto path = state.paths->autosavePath() / "snapshot.cupuacu-autosave";
    REQUIRE(cupuacu::persistence::saveDocumentAutosaveSnapshot(
        path, state.getActiveDocumentSession()));

    cupuacu::DocumentSession restored;
    REQUIRE(cupuacu::persistence::loadDocumentAutosaveSnapshot(path, restored));
    REQUIRE(restored.currentFile.empty());
    REQUIRE(restored.document.getSampleRate() == 44100);
    REQUIRE(restored.document.getChannelCount() == 1);
    REQUIRE(restored.document.getFrameCount() == 3);
    REQUIRE(restored.document.getSample(0, 0) == Catch::Approx(0.25f));
    REQUIRE(restored.document.getSample(0, 1) == Catch::Approx(-0.5f));
    REQUIRE(restored.document.getSample(0, 2) == Catch::Approx(0.75f));
    REQUIRE(restored.document.getMarkers().size() == 1);
    REQUIRE(restored.document.getMarkers()[0].frame == 2);
    REQUIRE(restored.document.getMarkers()[0].label == "point");
    requireBuildStatesEqual(
        cacheState, restored.getWaveformCache(0).snapshotBuildState());
    REQUIRE_FALSE(restored.getWaveformCacheBuildProgress().has_value());
}

TEST_CASE("Document autosave snapshot load reports frame progress", "[autosave]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("document-autosave");
    cupuacu::test::StateWithTestPaths state{root};
    std::vector<float> samples(70000, 0.0f);
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        samples[i] = static_cast<float>(i % 1024) / 1024.0f;
    }
    initializeMonoDocument(state, samples);

    const auto path = state.paths->autosavePath() / "progress.cupuacu-autosave";
    REQUIRE(cupuacu::persistence::saveDocumentAutosaveSnapshot(
        path, state.getActiveDocumentSession()));

    cupuacu::DocumentSession restored;
    std::vector<double> progressValues;
    REQUIRE(cupuacu::persistence::loadDocumentAutosaveSnapshot(
        path, restored,
        [&](const std::optional<double> progress)
        {
            REQUIRE(progress.has_value());
            progressValues.push_back(*progress);
        }));
    REQUIRE_FALSE(progressValues.empty());
    REQUIRE(progressValues.front() == Catch::Approx(0.0));
    REQUIRE(progressValues.back() == Catch::Approx(1.0));
    REQUIRE(progressValues.size() >= 3);
}

TEST_CASE("Paths place runtime autosave and undo state outside config",
          "[autosave]")
{
    cupuacu::test::StateWithTestPaths state{
        cupuacu::test::makeUniqueTestRoot("document-autosave")};

    REQUIRE(state.paths->configPath().filename() == "config");
    REQUIRE(state.paths->statePath().filename() == "state");
    REQUIRE(state.paths->autosavePath().parent_path() == state.paths->statePath());
    REQUIRE(state.paths->autosavePath().filename() == "autosave");
    REQUIRE(state.paths->waveformCachePath().parent_path() ==
            state.paths->statePath());
    REQUIRE(state.paths->waveformCachePath().filename() == "waveform-cache");
    REQUIRE(state.paths->undoPath().parent_path() == state.paths->statePath());
    REQUIRE(state.paths->undoPath().filename() == "undo");
    REQUIRE(state.paths->clipboardPath().parent_path() == state.paths->statePath());
    REQUIRE(state.paths->clipboardPath().filename() == "clipboard");
}

TEST_CASE("Undoable mutations autosave and restore untitled sessions",
          "[autosave]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("document-autosave");
    {
        cupuacu::test::StateWithTestPaths state{root};
        initializeMonoDocument(state, {0.0f, 0.0f, 0.0f});
        state.getActiveDocumentSession().document.addMarker(1, "middle");

        state.addAndDoUndoable(
            std::make_shared<SetSampleUndoable>(&state, 2, 0.75f));
        state.getActiveDocumentSession().rebuildWaveformCacheSynchronously();
        drainPendingAutosave(state);

        const auto persisted = cupuacu::persistence::SessionStatePersistence::load(
            state.paths->sessionStatePath());
        REQUIRE(persisted.openDocuments.size() == 1);
        REQUIRE(persisted.openDocuments[0].filePath.empty());
        REQUIRE_FALSE(persisted.openDocuments[0].autosaveSnapshotPath.empty());
        REQUIRE(std::filesystem::exists(
            persisted.openDocuments[0].autosaveSnapshotPath));
    }

    cupuacu::test::StateWithTestPaths restored{root};
    const auto persisted = cupuacu::persistence::SessionStatePersistence::load(
        restored.paths->sessionStatePath());
    cupuacu::actions::restoreStartupDocument(&restored, {}, persisted);

    const auto &session = restored.getActiveDocumentSession();
    REQUIRE(session.currentFile.empty());
    REQUIRE(session.document.getFrameCount() == 3);
    REQUIRE(session.document.getSample(0, 2) == Catch::Approx(0.75f));
    REQUIRE(session.document.getMarkers().size() == 1);
    REQUIRE(session.document.getMarkers()[0].label == "middle");
    REQUIRE_FALSE(session.getWaveformCacheBuildProgress().has_value());
    const auto restoredCacheState = session.getWaveformCache(0).snapshotBuildState();
    REQUIRE(restoredCacheState.numSamples == 3);
    REQUIRE(restoredCacheState.dirtyToBlock < restoredCacheState.dirtyFromBlock);
    REQUIRE_FALSE(restoredCacheState.levels.empty());
}

TEST_CASE("Autosaved file-backed sessions restore the snapshot over the source",
          "[autosave]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("document-autosave");
    const auto sourcePath = root / "source.wav";
    {
        cupuacu::test::StateWithTestPaths state{root};
        initializeMonoDocument(state, {0.0f, 0.0f});
        state.getActiveDocumentSession().setCurrentFile(sourcePath.string());

        state.addAndDoUndoable(
            std::make_shared<SetSampleUndoable>(&state, 1, -0.5f));
        drainPendingAutosave(state);
    }

    cupuacu::test::StateWithTestPaths restored{root};
    const auto persisted = cupuacu::persistence::SessionStatePersistence::load(
        restored.paths->sessionStatePath());
    REQUIRE(persisted.openDocuments.size() == 1);
    REQUIRE(persisted.openDocuments[0].filePath == sourcePath.string());
    REQUIRE_FALSE(persisted.openDocuments[0].autosaveSnapshotPath.empty());

    cupuacu::actions::restoreStartupDocument(&restored, {}, persisted);
    const auto &session = restored.getActiveDocumentSession();
    REQUIRE(session.currentFile == sourcePath.string());
    REQUIRE(session.document.getFrameCount() == 2);
    REQUIRE(session.document.getSample(0, 1) == Catch::Approx(-0.5f));
    REQUIRE(cupuacu::actions::documentTabTitle(*restored.getActiveTab()) ==
            "source.wav*");
}

TEST_CASE("Shutdown flush persists dirty file-backed sessions for restart",
          "[autosave]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("document-autosave");
    const auto sourcePath = root / "source.wav";
    {
        cupuacu::test::StateWithTestPaths state{root};
        initializeMonoDocument(state, {0.0f, 0.0f});
        state.getActiveDocumentSession().setCurrentFile(sourcePath.string());

        state.addAndDoUndoable(
            std::make_shared<SetSampleUndoable>(&state, 1, -0.5f));
        REQUIRE(state.backgroundAutosaveJob != nullptr);

        cupuacu::actions::flushAutosaveSnapshotsForShutdown(&state);
        cupuacu::actions::persistSessionState(&state);

        const auto persisted =
            cupuacu::persistence::SessionStatePersistence::load(
                state.paths->sessionStatePath());
        REQUIRE(persisted.openDocuments.size() == 1);
        REQUIRE_FALSE(persisted.openDocuments[0].autosaveSnapshotPath.empty());
        REQUIRE(std::filesystem::exists(
            persisted.openDocuments[0].autosaveSnapshotPath));
    }

    cupuacu::test::StateWithTestPaths restored{root};
    const auto persisted = cupuacu::persistence::SessionStatePersistence::load(
        restored.paths->sessionStatePath());
    cupuacu::actions::restoreStartupDocument(&restored, {}, persisted);

    const auto &session = restored.getActiveDocumentSession();
    REQUIRE(session.currentFile == sourcePath.string());
    REQUIRE(session.document.getSample(0, 1) == Catch::Approx(-0.5f));
    REQUIRE(cupuacu::actions::documentTabTitle(*restored.getActiveTab()) ==
            "source.wav*");
}

TEST_CASE("Shutdown flush does not rewrite current restored autosave snapshots",
          "[autosave]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("document-autosave");
    const auto sourcePath = root / "source.wav";
    std::filesystem::path autosavePath;
    std::int64_t originalWriteTimeNs = 0;
    {
        cupuacu::test::StateWithTestPaths state{root};
        initializeMonoDocument(state, {0.0f, 0.0f});
        state.getActiveDocumentSession().setCurrentFile(sourcePath.string());

        state.addAndDoUndoable(
            std::make_shared<SetSampleUndoable>(&state, 1, -0.5f));
        state.getActiveDocumentSession().cursor = 1;
        REQUIRE(cupuacu::actions::markers::insertMarkerAtCursor(&state) != 0);
        drainPendingAutosave(state);

        autosavePath = state.getActiveDocumentSession().autosaveSnapshotPath;
        REQUIRE_FALSE(autosavePath.empty());
        REQUIRE(std::filesystem::exists(autosavePath));
        originalWriteTimeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::filesystem::last_write_time(autosavePath)
                                      .time_since_epoch())
                                  .count();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    cupuacu::test::StateWithTestPaths restored{root};
    const auto persisted = cupuacu::persistence::SessionStatePersistence::load(
        restored.paths->sessionStatePath());
    cupuacu::actions::restoreStartupDocument(&restored, {}, persisted);

    cupuacu::actions::flushAutosaveSnapshotsForShutdown(&restored);
    REQUIRE(std::filesystem::exists(autosavePath));
    const auto flushedWriteTimeNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::filesystem::last_write_time(autosavePath).time_since_epoch())
            .count();
    REQUIRE(flushedWriteTimeNs == originalWriteTimeNs);
}

TEST_CASE("Background autosave job fails if the snapshot path changes",
          "[autosave]")
{
    cupuacu::test::StateWithTestPaths state{
        cupuacu::test::makeUniqueTestRoot("document-autosave")};
    initializeMonoDocument(state, {0.0f, 0.5f, 1.0f});

    auto &session = state.getActiveDocumentSession();
    const auto originalPath =
        state.paths->autosavePath() / "original.cupuacu-autosave";
    session.autosaveSnapshotPath = originalPath;

    cupuacu::actions::io::BackgroundAutosaveJob job(
        state.getActiveTab()->id, originalPath,
        session.document.getWaveformDataVersion(),
        session.document.getMarkerDataVersion(), session.currentFile);

    session.autosaveSnapshotPath =
        state.paths->autosavePath() / "moved.cupuacu-autosave";
    job.pump(&state);

    const auto snapshot = job.snapshot();
    REQUIRE(snapshot.completed);
    REQUIRE_FALSE(snapshot.success);
    REQUIRE(snapshot.error == "Autosave snapshot path changed");
    REQUIRE_FALSE(std::filesystem::exists(originalPath));
}

TEST_CASE("Background autosave job fails if the source file changes",
          "[autosave]")
{
    cupuacu::test::StateWithTestPaths state{
        cupuacu::test::makeUniqueTestRoot("document-autosave")};
    initializeMonoDocument(state, {0.0f, 0.5f, 1.0f});

    auto &session = state.getActiveDocumentSession();
    session.setCurrentFile("/tmp/original.wav");
    const auto autosavePath =
        state.paths->autosavePath() / "source-change.cupuacu-autosave";
    session.autosaveSnapshotPath = autosavePath;

    cupuacu::actions::io::BackgroundAutosaveJob job(
        state.getActiveTab()->id, autosavePath,
        session.document.getWaveformDataVersion(),
        session.document.getMarkerDataVersion(), session.currentFile);

    session.currentFile = "/tmp/renamed.wav";
    job.pump(&state);

    const auto snapshot = job.snapshot();
    REQUIRE(snapshot.completed);
    REQUIRE_FALSE(snapshot.success);
    REQUIRE(snapshot.error == "Autosave source file changed");
    REQUIRE_FALSE(std::filesystem::exists(autosavePath));
}

TEST_CASE("Stale autosave jobs are retried with the latest document version",
          "[autosave]")
{
    cupuacu::test::StateWithTestPaths state{
        cupuacu::test::makeUniqueTestRoot("document-autosave")};
    initializeMonoDocument(state, {0.0f, 0.0f, 0.0f, 0.0f});

    auto &session = state.getActiveDocumentSession();
    session.autosaveSnapshotPath =
        state.paths->autosavePath() / "retry-stale.cupuacu-autosave";
    const auto queuedWaveformVersion = session.document.getWaveformDataVersion();
    const auto queuedMarkerVersion = session.document.getMarkerDataVersion();

    cupuacu::actions::io::queueAutosaveForTab(&state, 0);
    REQUIRE(state.backgroundAutosaveJob != nullptr);
    REQUIRE(state.backgroundAutosaveJob->snapshot().waveformDataVersion ==
            queuedWaveformVersion);

    session.document.setSample(0, 1, 0.75f, false);
    const auto updatedWaveformVersion = session.document.getWaveformDataVersion();
    REQUIRE(updatedWaveformVersion != queuedWaveformVersion);

    cupuacu::actions::io::processPendingAutosaveWork(&state);

    REQUIRE(state.backgroundAutosaveJob != nullptr);
    const auto retrySnapshot = state.backgroundAutosaveJob->snapshot();
    REQUIRE(retrySnapshot.waveformDataVersion == updatedWaveformVersion);
    REQUIRE(retrySnapshot.markerDataVersion == queuedMarkerVersion);

    drainPendingAutosave(state);

    REQUIRE_FALSE(state.backgroundAutosaveJob);
    REQUIRE(std::filesystem::exists(session.autosaveSnapshotPath));
    REQUIRE(session.autosavedWaveformDataVersion == updatedWaveformVersion);
    REQUIRE(session.autosavedMarkerDataVersion ==
            session.document.getMarkerDataVersion());

    const auto persisted = cupuacu::persistence::SessionStatePersistence::load(
        state.paths->sessionStatePath());
    REQUIRE(persisted.openDocuments.size() == 1);
    REQUIRE(persisted.openDocuments[0].autosaveSnapshotPath ==
            session.autosaveSnapshotPath.string());
}

TEST_CASE("Undoing file-backed edits back to the open-file baseline clears autosave",
          "[autosave]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("document-autosave");
    cupuacu::test::StateWithTestPaths state{root};
    const auto sourcePath = root / "source.wav";
    initializeMonoDocument(state, {0.0f, 0.0f, 0.0f});
    state.getActiveDocumentSession().setCurrentFile(sourcePath.string());

    state.addAndDoUndoable(
        std::make_shared<SetSampleUndoable>(&state, 1, -0.5f));
    drainPendingAutosave(state);

    const auto autosavePath =
        state.getActiveDocumentSession().autosaveSnapshotPath;
    REQUIRE_FALSE(autosavePath.empty());
    REQUIRE(std::filesystem::exists(autosavePath));

    state.undo();

    REQUIRE(state.getActiveUndoables().empty());
    REQUIRE(state.getActiveDocumentSession().autosaveSnapshotPath.empty());
    REQUIRE_FALSE(std::filesystem::exists(autosavePath));

    const auto persisted = cupuacu::persistence::SessionStatePersistence::load(
        state.paths->sessionStatePath());
    REQUIRE(persisted.openDocuments.size() == 1);
    REQUIRE(persisted.openDocuments[0].filePath == sourcePath.string());
    REQUIRE(persisted.openDocuments[0].autosaveSnapshotPath.empty());
}

TEST_CASE("Undo store is created for edited sessions and cleared on close",
          "[autosave]")
{
    cupuacu::test::StateWithTestPaths state{
        cupuacu::test::makeUniqueTestRoot("document-autosave")};
    initializeMonoDocument(state, {0.0f, 0.0f, 0.0f});

    state.addAndDoUndoable(
        std::make_shared<SetSampleUndoable>(&state, 1, -0.5f));

    const auto undoRoot = state.getActiveDocumentSession().undoStore.root();
    REQUIRE(state.getActiveDocumentSession().undoStore.isAttached());
    REQUIRE_FALSE(undoRoot.empty());
    REQUIRE(std::filesystem::exists(undoRoot));

    cupuacu::actions::closeCurrentDocument(&state, false);

    REQUIRE_FALSE(state.getActiveDocumentSession().undoStore.isAttached());
    REQUIRE_FALSE(std::filesystem::exists(undoRoot));
}

TEST_CASE("Undo store stats report payload count and bytes",
          "[autosave]")
{
    cupuacu::test::StateWithTestPaths state{
        cupuacu::test::makeUniqueTestRoot("document-autosave")};
    initializeMonoDocument(state, {0.0f, 1.0f, 2.0f, 3.0f});

    state.addAndDoUndoable(
        std::make_shared<SetSampleUndoable>(&state, 1, -0.5f));

    const auto emptyStats = state.getActiveDocumentSession().undoStore.stats();
    REQUIRE(emptyStats.fileCount == 0);
    REQUIRE(emptyStats.totalBytes == 0);

    cupuacu::Document::AudioSegment segment =
        state.getActiveDocumentSession().document.captureSegment(1, 2);
    const auto handle =
        state.getActiveDocumentSession().undoStore.writeSegment(segment, "stats");
    REQUIRE_FALSE(handle.empty());

    const auto stats = state.getActiveDocumentSession().undoStore.stats();
    REQUIRE(stats.fileCount == 1);
    REQUIRE(stats.totalBytes > 0);
}

TEST_CASE("Undo segments store contiguous provenance compactly",
          "[autosave]")
{
    cupuacu::test::StateWithTestPaths state{
        cupuacu::test::makeUniqueTestRoot("document-autosave")};
    std::vector<float> samples(1000, 0.0f);
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        samples[i] = static_cast<float>(i) / 1000.0f;
    }
    initializeMonoDocument(state, samples);

    const auto segment =
        state.getActiveDocumentSession().document.captureSegment(
            0, state.getActiveDocumentSession().document.getFrameCount());
    state.getActiveDocumentSession().undoStore.attach(
        state.paths->undoPath() / "compact-provenance-store");
    const auto handle =
        state.getActiveDocumentSession().undoStore.writeSegment(
            segment, "compact-provenance");
    REQUIRE_FALSE(handle.empty());

    const auto restored =
        state.getActiveDocumentSession().undoStore.readSegment(handle);
    REQUIRE(restored.frameCount == segment.frameCount);
    REQUIRE(restored.channelCount == segment.channelCount);
    for (std::size_t channel = 0; channel < restored.provenance.size(); ++channel)
    {
        REQUIRE(restored.provenance[channel].size() ==
                segment.provenance[channel].size());
        for (std::size_t frame = 0; frame < restored.provenance[channel].size();
             ++frame)
        {
            REQUIRE(restored.provenance[channel][frame].sourceId ==
                    segment.provenance[channel][frame].sourceId);
            REQUIRE(restored.provenance[channel][frame].frameIndex ==
                    segment.provenance[channel][frame].frameIndex);
        }
    }

    const auto size = std::filesystem::file_size(handle.path);
    REQUIRE(size < 6000);
}

TEST_CASE("Restart undo persistence byte policy rejects oversized stores",
          "[autosave]")
{
    REQUIRE(cupuacu::undo::shouldPersistUndoStoreForRestart(
        {.fileCount = 1, .totalBytes = 0}, 1));
    REQUIRE(cupuacu::undo::shouldPersistUndoStoreForRestart(
        {.fileCount = 2, .totalBytes = 128}, 128));
    REQUIRE_FALSE(cupuacu::undo::shouldPersistUndoStoreForRestart(
        {.fileCount = 2, .totalBytes = 129}, 128));
}

TEST_CASE("Startup restore preserves persistent cut undo history", "[autosave]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("document-autosave");
    {
        cupuacu::test::StateWithTestPaths state{root};
        initializeMonoDocument(state, {0.0f, 1.0f, 2.0f, 3.0f});
        auto &session = state.getActiveDocumentSession();
        session.selection.setValue1(1.0);
        session.selection.setValue2(3.0);

        cupuacu::actions::audio::performCut(&state);
        drainPendingAutosave(state);
        cupuacu::actions::persistSessionState(&state);

        const auto persisted = cupuacu::persistence::SessionStatePersistence::load(
            state.paths->sessionStatePath());
        REQUIRE(persisted.openDocuments.size() == 1);
        REQUIRE_FALSE(persisted.openDocuments[0].autosaveSnapshotPath.empty());
        REQUIRE_FALSE(persisted.openDocuments[0].undoStorePath.empty());
        REQUIRE(std::filesystem::exists(
            persisted.openDocuments[0].autosaveSnapshotPath));
        REQUIRE(std::filesystem::exists(persisted.openDocuments[0].undoStorePath));
    }

    cupuacu::test::StateWithTestPaths restored{root};
    const auto persisted = cupuacu::persistence::SessionStatePersistence::load(
        restored.paths->sessionStatePath());
    cupuacu::actions::restoreStartupDocument(&restored, {}, persisted);

    REQUIRE(readMonoSamples(restored.getActiveDocumentSession().document) ==
            std::vector<float>({0.0f, 3.0f}));
    REQUIRE(restored.canUndo());

    restored.undo();
    REQUIRE(readMonoSamples(restored.getActiveDocumentSession().document) ==
            std::vector<float>({0.0f, 1.0f, 2.0f, 3.0f}));
}

TEST_CASE("Startup restore preserves persistent sample edit undo history",
          "[autosave]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("document-autosave");
    {
        cupuacu::test::StateWithTestPaths state{root};
        initializeMonoDocument(state, {0.0f, 1.0f, 2.0f});

        auto undoable =
            std::make_shared<cupuacu::actions::audio::SetSampleValue>(
                &state, 0, 1, 1.0f);
        undoable->setNewValue(9.0f);
        state.addAndDoUndoable(undoable);

        drainPendingAutosave(state);
        cupuacu::actions::persistSessionState(&state);
    }

    cupuacu::test::StateWithTestPaths restored{root};
    const auto persisted = cupuacu::persistence::SessionStatePersistence::load(
        restored.paths->sessionStatePath());
    cupuacu::actions::restoreStartupDocument(&restored, {}, persisted);

    REQUIRE(readMonoSamples(restored.getActiveDocumentSession().document) ==
            std::vector<float>({0.0f, 9.0f, 2.0f}));
    REQUIRE(restored.canUndo());

    restored.undo();
    REQUIRE(readMonoSamples(restored.getActiveDocumentSession().document) ==
            std::vector<float>({0.0f, 1.0f, 2.0f}));
}

TEST_CASE("Startup restore preserves persistent marker edit undo history",
          "[autosave]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("document-autosave");
    uint64_t markerId = 0;
    {
        cupuacu::test::StateWithTestPaths state{root};
        initializeMonoDocument(state, {0.0f, 1.0f, 2.0f});
        state.getActiveDocumentSession().cursor = 2;
        markerId = cupuacu::actions::markers::insertMarkerAtCursor(&state);

        drainPendingAutosave(state);
        cupuacu::actions::persistSessionState(&state);
    }

    cupuacu::test::StateWithTestPaths restored{root};
    const auto persisted = cupuacu::persistence::SessionStatePersistence::load(
        restored.paths->sessionStatePath());
    cupuacu::actions::restoreStartupDocument(&restored, {}, persisted);

    REQUIRE(restored.getActiveDocumentSession().document.getMarkers().size() == 1);
    REQUIRE(restored.canUndo());

    restored.undo();
    REQUIRE(restored.getActiveDocumentSession().document.getMarkers().empty());
    REQUIRE(restored.getActiveViewState().selectedMarkerId == std::nullopt);
    REQUIRE(markerId != 0);
}

TEST_CASE("Startup restore preserves clipboard and copy undo history",
          "[autosave]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("document-autosave");
    {
        cupuacu::test::StateWithTestPaths state{root};
        initializeMonoDocument(state, {0.0f, 1.0f, 2.0f});
        auto &session = state.getActiveDocumentSession();
        session.selection.setValue1(0.0);
        session.selection.setValue2(2.0);
        cupuacu::actions::audio::performCopy(&state);

        const auto autosavePath =
            state.paths->autosavePath() / "clipboard-copy-doc.cupuacu-autosave";
        REQUIRE(cupuacu::persistence::saveDocumentAutosaveSnapshot(
            autosavePath, session));
        session.autosaveSnapshotPath = autosavePath;

        cupuacu::actions::persistSessionState(&state);
        const auto persisted = cupuacu::persistence::SessionStatePersistence::load(
            state.paths->sessionStatePath());
        REQUIRE_FALSE(persisted.clipboardSnapshotPath.empty());
        REQUIRE(std::filesystem::exists(persisted.clipboardSnapshotPath));
        REQUIRE(persisted.openDocuments.size() == 1);
        REQUIRE_FALSE(persisted.openDocuments[0].undoStorePath.empty());
    }

    cupuacu::test::StateWithTestPaths restored{root};
    const auto persisted = cupuacu::persistence::SessionStatePersistence::load(
        restored.paths->sessionStatePath());
    cupuacu::actions::restoreStartupDocument(&restored, {}, persisted);

    REQUIRE(readMonoSamples(restored.clipboard) == std::vector<float>({0.0f, 1.0f}));
    REQUIRE(restored.canUndo());
    REQUIRE(restored.getUndoDescription() == "Copy");

    restored.getActiveDocumentSession().selection.reset();
    restored.getActiveDocumentSession().cursor = 3;
    cupuacu::actions::audio::performPaste(&restored);
    REQUIRE(readMonoSamples(restored.getActiveDocumentSession().document) ==
            std::vector<float>({0.0f, 1.0f, 2.0f, 0.0f, 1.0f}));
}

TEST_CASE("Startup restore preserves multi-step persistent cut undo history",
          "[autosave]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("document-autosave");
    {
        cupuacu::test::StateWithTestPaths state{root};
        initializeMonoDocument(state, {0.0f, 1.0f, 2.0f, 3.0f});
        auto &session = state.getActiveDocumentSession();

        session.selection.setValue1(1.0);
        session.selection.setValue2(2.0);
        cupuacu::actions::audio::performCut(&state);

        session.selection.setValue1(2.0);
        session.selection.setValue2(3.0);
        cupuacu::actions::audio::performCut(&state);

        drainPendingAutosave(state);
        cupuacu::actions::persistSessionState(&state);
    }

    cupuacu::test::StateWithTestPaths restored{root};
    const auto persisted = cupuacu::persistence::SessionStatePersistence::load(
        restored.paths->sessionStatePath());
    cupuacu::actions::restoreStartupDocument(&restored, {}, persisted);

    REQUIRE(readMonoSamples(restored.getActiveDocumentSession().document) ==
            std::vector<float>({0.0f, 2.0f}));
    REQUIRE(restored.canUndo());

    restored.undo();
    REQUIRE(readMonoSamples(restored.getActiveDocumentSession().document) ==
            std::vector<float>({0.0f, 2.0f, 3.0f}));
    REQUIRE(restored.canUndo());

    restored.undo();
    REQUIRE(readMonoSamples(restored.getActiveDocumentSession().document) ==
            std::vector<float>({0.0f, 1.0f, 2.0f, 3.0f}));
}

TEST_CASE("Startup restore preserves persistent redo history",
          "[autosave]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("document-autosave");
    {
        cupuacu::test::StateWithTestPaths state{root};
        initializeMonoDocument(state, {0.0f, 1.0f, 2.0f, 3.0f});
        auto &session = state.getActiveDocumentSession();
        session.selection.setValue1(1.0);
        session.selection.setValue2(3.0);

        cupuacu::actions::audio::performCut(&state);
        state.undo();
        REQUIRE(state.canRedo());

        drainPendingAutosave(state);
        cupuacu::actions::persistSessionState(&state);
    }

    cupuacu::test::StateWithTestPaths restored{root};
    const auto persisted = cupuacu::persistence::SessionStatePersistence::load(
        restored.paths->sessionStatePath());
    cupuacu::actions::restoreStartupDocument(&restored, {}, persisted);

    REQUIRE(readMonoSamples(restored.getActiveDocumentSession().document) ==
            std::vector<float>({0.0f, 1.0f, 2.0f, 3.0f}));
    REQUIRE_FALSE(restored.canUndo());
    REQUIRE(restored.canRedo());

    restored.redo();
    REQUIRE(readMonoSamples(restored.getActiveDocumentSession().document) ==
            std::vector<float>({0.0f, 3.0f}));
    REQUIRE(restored.canUndo());
    REQUIRE_FALSE(restored.canRedo());
}

TEST_CASE("Startup restore prunes stale undo stores and keeps active ones",
          "[autosave]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("document-autosave");
    std::filesystem::path activeUndoStore;
    {
        cupuacu::test::StateWithTestPaths state{root};
        initializeMonoDocument(state, {0.0f, 1.0f, 2.0f, 3.0f});
        auto &session = state.getActiveDocumentSession();
        session.selection.setValue1(1.0);
        session.selection.setValue2(3.0);
        cupuacu::actions::audio::performCut(&state);
        drainPendingAutosave(state);
        cupuacu::actions::persistSessionState(&state);
        activeUndoStore = session.undoStore.root();
    }

    cupuacu::test::StateWithTestPaths restored{root};
    const auto staleUndoStore = restored.paths->undoPath() / "stale-store";
    std::filesystem::create_directories(staleUndoStore);
    {
        std::ofstream(staleUndoStore / "orphan.bin") << "orphan";
    }
    REQUIRE(std::filesystem::exists(activeUndoStore));
    REQUIRE(std::filesystem::exists(staleUndoStore));

    const auto persisted = cupuacu::persistence::SessionStatePersistence::load(
        restored.paths->sessionStatePath());
    cupuacu::actions::restoreStartupDocument(&restored, {}, persisted);

    REQUIRE(std::filesystem::exists(activeUndoStore));
    REQUIRE_FALSE(std::filesystem::exists(staleUndoStore));
    REQUIRE(restored.canUndo());
}

TEST_CASE("Undo manifest restore fails cleanly for unsupported entry kinds",
          "[autosave]")
{
    cupuacu::test::StateWithTestPaths state{
        cupuacu::test::makeUniqueTestRoot("document-autosave")};
    initializeMonoDocument(state, {0.0f, 1.0f, 2.0f});

    const auto undoStorePath = state.paths->undoPath() / "unsupported-kind";
    std::filesystem::create_directories(undoStorePath);
    const auto manifestPath =
        cupuacu::undo::manifestPathForStore(undoStorePath);

    nlohmann::json json{
        {"version", 1},
        {"entries",
         nlohmann::json::array({nlohmann::json{{"kind", "not-supported"}}})},
    };
    {
        std::ofstream output(manifestPath);
        REQUIRE(output.is_open());
        output << json.dump(2) << '\n';
    }

    REQUIRE_FALSE(cupuacu::undo::restoreUndoManifest(&state, 0, undoStorePath));
    REQUIRE(state.getActiveUndoables().empty());
    REQUIRE(state.getActiveRedoables().empty());
}

TEST_CASE("Undo manifest restore fails cleanly for unsupported versions",
          "[autosave]")
{
    cupuacu::test::StateWithTestPaths state{
        cupuacu::test::makeUniqueTestRoot("document-autosave")};
    initializeMonoDocument(state, {0.0f, 1.0f, 2.0f});

    const auto undoStorePath = state.paths->undoPath() / "unsupported-version";
    std::filesystem::create_directories(undoStorePath);
    const auto manifestPath =
        cupuacu::undo::manifestPathForStore(undoStorePath);

    nlohmann::json json{
        {"version", 999},
        {"entries", nlohmann::json::array()},
    };
    {
        std::ofstream output(manifestPath);
        REQUIRE(output.is_open());
        output << json.dump(2) << '\n';
    }

    REQUIRE_FALSE(cupuacu::undo::restoreUndoManifest(&state, 0, undoStorePath));
    REQUIRE(state.getActiveUndoables().empty());
    REQUIRE(state.getActiveRedoables().empty());
}

TEST_CASE("Startup restore reports missing clipboard state",
          "[autosave]")
{
    cupuacu::test::StateWithTestPaths state{
        cupuacu::test::makeUniqueTestRoot("document-autosave")};
    cupuacu::persistence::PersistedSessionState persisted{};
    persisted.clipboardSnapshotPath =
        (state.paths->clipboardPath() / "missing.cupuacu-clipboard").string();

    std::string title;
    std::string message;
    state.errorReporter = [&](const std::string &reportedTitle,
                              const std::string &reportedMessage)
    {
        title = reportedTitle;
        message = reportedMessage;
    };

    cupuacu::actions::restoreStartupDocument(&state, {}, persisted);

    REQUIRE(title == "Some session state could not be restored");
    REQUIRE(message.find("Clipboard from the previous session") !=
            std::string::npos);
}

TEST_CASE("Startup restore reports dropped undo history",
          "[autosave]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("document-autosave");
    {
        cupuacu::test::StateWithTestPaths state{root};
        initializeMonoDocument(state, {0.0f, 1.0f, 2.0f, 3.0f});
        auto &session = state.getActiveDocumentSession();
        session.selection.setValue1(1.0);
        session.selection.setValue2(3.0);
        cupuacu::actions::audio::performCut(&state);
        drainPendingAutosave(state);
        cupuacu::actions::persistSessionState(&state);

        const auto persisted = cupuacu::persistence::SessionStatePersistence::load(
            state.paths->sessionStatePath());
        REQUIRE(persisted.openDocuments.size() == 1);
        REQUIRE_FALSE(persisted.openDocuments[0].undoStorePath.empty());
        std::filesystem::remove(
            cupuacu::undo::manifestPathForStore(
                persisted.openDocuments[0].undoStorePath));
    }

    cupuacu::test::StateWithTestPaths restored{root};
    std::string title;
    std::string message;
    restored.errorReporter = [&](const std::string &reportedTitle,
                                 const std::string &reportedMessage)
    {
        title = reportedTitle;
        message = reportedMessage;
    };

    const auto persisted = cupuacu::persistence::SessionStatePersistence::load(
        restored.paths->sessionStatePath());
    cupuacu::actions::restoreStartupDocument(&restored, {}, persisted);

    REQUIRE(title == "Some session state could not be restored");
    REQUIRE(message.find("undo/redo history") != std::string::npos);
}

TEST_CASE("Undo manifest restore fails cleanly for missing payload files",
          "[autosave]")
{
    cupuacu::test::StateWithTestPaths state{
        cupuacu::test::makeUniqueTestRoot("document-autosave")};
    initializeMonoDocument(state, {0.0f, 1.0f, 2.0f});

    const auto undoStorePath = state.paths->undoPath() / "missing-payload";
    std::filesystem::create_directories(undoStorePath);
    const auto manifestPath =
        cupuacu::undo::manifestPathForStore(undoStorePath);

    nlohmann::json json{
        {"version", 1},
        {"entries",
         nlohmann::json::array({nlohmann::json{
             {"kind", "cut"},
             {"startFrame", 1},
             {"frameCount", 1},
             {"removedHandle",
              (undoStorePath / "missing.cupuacu-undo-segment").string()},
             {"oldSelectionStart", 1.0},
             {"oldSelectionEnd", 2.0},
             {"oldCursorPos", 2},
         }})},
    };
    {
        std::ofstream output(manifestPath);
        REQUIRE(output.is_open());
        output << json.dump(2) << '\n';
    }

    REQUIRE_FALSE(cupuacu::undo::restoreUndoManifest(&state, 0, undoStorePath));
    REQUIRE(state.getActiveUndoables().empty());
    REQUIRE(state.getActiveRedoables().empty());
}

TEST_CASE("Session-only undoables do not persist restart undo history",
          "[autosave]")
{
    cupuacu::test::StateWithTestPaths state{
        cupuacu::test::makeUniqueTestRoot("document-autosave")};
    initializeMonoDocument(state, {0.0f, 1.0f, 2.0f});

    auto undoable = std::make_shared<SetSampleUndoable>(&state, 1, 9.0f);
    state.addAndDoUndoable(undoable);
    drainPendingAutosave(state);
    cupuacu::actions::persistSessionState(&state);

    const auto persisted = cupuacu::persistence::SessionStatePersistence::load(
        state.paths->sessionStatePath());
    REQUIRE(persisted.openDocuments.size() == 1);
    REQUIRE(persisted.openDocuments[0].undoStorePath.empty());
}
