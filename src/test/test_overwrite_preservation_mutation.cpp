#include <catch2/catch_test_macros.hpp>

#include "DocumentSession.hpp"
#include "State.hpp"
#include "TestPaths.hpp"
#include "actions/Undoable.hpp"
#include "actions/audio/RecordedChunkApplier.hpp"
#include "audio/RecordedChunk.hpp"
#include "file/OverwritePreservationMutation.hpp"

namespace
{
    class IncompatibleUndoable : public cupuacu::actions::Undoable
    {
    public:
        explicit IncompatibleUndoable(cupuacu::State *state) : Undoable(state) {}

        void redo() override {}
        void undo() override {}

        std::string getRedoDescription() override
        {
            return "Incompatible";
        }

        std::string getUndoDescription() override
        {
            return "Incompatible";
        }

        [[nodiscard]] cupuacu::file::OverwritePreservationMutation
        overwritePreservationMutation() const override
        {
            return cupuacu::file::OverwritePreservationMutationHelper::
                incompatible("Test incompatibility");
        }
    };
} // namespace

TEST_CASE("Overwrite preservation mutation helper marks session incompatible",
          "[file]")
{
    cupuacu::DocumentSession session{};

    const auto mutation =
        cupuacu::file::OverwritePreservationMutationHelper::incompatible(
            "Sample rate changed");
    cupuacu::file::OverwritePreservationMutationHelper::applyToSession(
        session, mutation);

    REQUIRE(session.overwritePreservationBrokenByOperation);
    REQUIRE(session.overwritePreservationBrokenReason == "Sample rate changed");

    cupuacu::file::OverwritePreservationMutationHelper::revertOnSession(
        session, mutation);

    REQUIRE_FALSE(session.overwritePreservationBrokenByOperation);
    REQUIRE(session.overwritePreservationBrokenReason.empty());
}

TEST_CASE("Overwrite preservation mutation helper leaves compatible session unchanged",
          "[file]")
{
    cupuacu::DocumentSession session{};
    session.breakOverwritePreservation("Existing incompatibility");

    const auto mutation =
        cupuacu::file::OverwritePreservationMutationHelper::compatible();
    cupuacu::file::OverwritePreservationMutationHelper::applyToSession(
        session, mutation);

    REQUIRE(session.overwritePreservationBrokenByOperation);
    REQUIRE(session.overwritePreservationBrokenReason ==
            "Existing incompatibility");
}

TEST_CASE("State applies undoable preservation mutation on redo and undo",
          "[file]")
{
    cupuacu::test::StateWithTestPaths state{};

    state.addAndDoUndoable(std::make_shared<IncompatibleUndoable>(&state));

    auto &session = state.getActiveDocumentSession();
    REQUIRE(session.overwritePreservationBrokenByOperation);
    REQUIRE(session.overwritePreservationBrokenReason == "Test incompatibility");

    state.undo();

    REQUIRE_FALSE(session.overwritePreservationBrokenByOperation);
    REQUIRE(session.overwritePreservationBrokenReason.empty());

    state.redo();

    REQUIRE(session.overwritePreservationBrokenByOperation);
    REQUIRE(session.overwritePreservationBrokenReason == "Test incompatibility");
}

TEST_CASE("Recorded chunk applier reports incompatible mutation when channel count expands",
          "[file]")
{
    cupuacu::Document document{};
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 4);

    cupuacu::audio::RecordedChunk chunk{};
    chunk.startFrame = 1;
    chunk.frameCount = 2;
    chunk.channelCount = 2;
    chunk.interleavedSamples[0] = 0.25f;
    chunk.interleavedSamples[1] = -0.25f;
    chunk.interleavedSamples[2] = 0.5f;
    chunk.interleavedSamples[3] = -0.5f;

    const auto result =
        cupuacu::actions::audio::applyRecordedChunk(document, chunk);

    REQUIRE(result.channelLayoutChanged);
    REQUIRE(result.preservationMutation.impact ==
            cupuacu::file::OverwritePreservationMutationImpact::Incompatible);
    REQUIRE(result.preservationMutation.reason == "Recording changed channel count");
}
