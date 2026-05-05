#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestPaths.hpp"
#include "actions/DocumentRestore.hpp"
#include "actions/DocumentSessionPersistence.hpp"
#include "actions/DocumentLifecycle.hpp"
#include "actions/DocumentUi.hpp"
#include "actions/audio/EditCommands.hpp"
#include "actions/io/BackgroundSave.hpp"
#include "actions/Undoable.hpp"
#include "persistence/DocumentAutosave.hpp"
#include "persistence/SessionStatePersistence.hpp"
#include "undo/UndoManifestPersistence.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
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

    std::vector<float> readMonoSamples(const cupuacu::Document &document)
    {
        std::vector<float> result(
            static_cast<std::size_t>(document.getFrameCount()));
        for (int64_t i = 0; i < document.getFrameCount(); ++i)
        {
            result[static_cast<std::size_t>(i)] = document.getSample(0, i);
        }
        return result;
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
    REQUIRE(state.paths->undoPath().parent_path() == state.paths->statePath());
    REQUIRE(state.paths->undoPath().filename() == "undo");
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
