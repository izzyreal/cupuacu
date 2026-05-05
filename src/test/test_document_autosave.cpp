#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestPaths.hpp"
#include "actions/DocumentLifecycle.hpp"
#include "actions/io/BackgroundSave.hpp"
#include "actions/Undoable.hpp"
#include "persistence/DocumentAutosave.hpp"
#include "persistence/SessionStatePersistence.hpp"

#include <filesystem>
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
