#pragma once

#include "../Undoable.hpp"
#include "../../storage/AudioEditRevision.hpp"
#include "../../storage/WorkingAllocator.hpp"

namespace cupuacu::actions::audio
{
    // Command snapshots contain roots and small editor metadata, never samples.
    struct RevisionEditState
    {
        std::shared_ptr<const storage::AudioEditRevision> audio;
        storage::WorkingVector<DocumentMarker, storage::MemoryUse::Index>
            markers;
        gui::Selection<double> selection{0.0};
        int64_t cursor = 0;
        static RevisionEditState capture(const DocumentSession &session);
    };

    class RevisionEdit final : public Undoable
    {
        friend class persistence::RevisionPersistence;
        uint64_t tabId;
        std::string description;
        RevisionEditState before, after;
        std::optional<ClipboardAudio> copiedAudio;
        bool committed = false;
        bool trimView;
        gui::EditorViewState beforeView{}, afterView{};
        bool haveAfterView = false, doingRedo = true;
        DocumentSession *target() const;
        void apply(bool redo);

    public:
        RevisionEdit(State *, int tabIndex, std::string description,
                     RevisionEditState before, RevisionEditState after,
                     std::optional<ClipboardAudio> copiedAudio = {},
                     bool trimView = false);
        void redo() override
        {
            apply(true);
        }
        void undo() override
        {
            apply(false);
        }
        std::string getRedoDescription() override
        {
            return description;
        }
        std::string getUndoDescription() override
        {
            return description;
        }
        bool lastOperationCommitted() const override
        {
            return committed;
        }
        file::OverwritePreservationMutation
        overwritePreservationMutation() const override
        {
            const auto old = before.audio->shape(), next = after.audio->shape();
            if (old.channels != next.channels ||
                old.sampleRate != next.sampleRate || old.format != next.format)
            {
                return file::OverwritePreservationMutationHelper::incompatible(
                    "Edit changed audio format");
            }
            return file::OverwritePreservationMutationHelper::compatible();
        }
    };

    enum class RevisionCommand
    {
        Copy,
        Cut,
        Delete,
        Paste,
        Trim,
        InsertSilence
    };
    void performRevisionCommand(
        State *, RevisionCommand, int64_t start, int64_t count,
        int64_t silenceFrames = 0,
        std::shared_ptr<const storage::AudioEditRevision> pasteSource = {},
        uint64_t targetTabId = 0);
    void processPendingRevisionCommands(State *);
    void prepareRevisionEdit(
        State *, std::string,
        std::function<RevisionEditState(const RevisionEditState &)>);
    void prepareRevisionSampleEdit(
        State *, std::shared_ptr<const storage::AudioEditRevision> expected,
        uint32_t channel, int64_t frame, float value);
    void prepareRevisionAction(State *,
                               std::function<std::function<void(State *, int)>(
                                   const RevisionEditState &)>);
    void removeMarkerRange(std::span<DocumentMarker>, int64_t start,
                           int64_t count);
} // namespace cupuacu::actions::audio
