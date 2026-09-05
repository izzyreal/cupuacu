#pragma once

#include "../Undoable.hpp"
#include "../../storage/AudioEditRevision.hpp"

namespace cupuacu::actions::audio
{
    // Command snapshots contain roots and small editor metadata, never samples.
    struct RevisionEditState
    {
        std::shared_ptr<const storage::AudioEditRevision> audio;
        std::vector<DocumentMarker> markers;
        gui::Selection<double> selection{0.0};
        int64_t cursor = 0;
        static RevisionEditState capture(const DocumentSession &session);
    };

    class RevisionEdit final : public Undoable
    {
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
    void performRevisionCommand(State *, RevisionCommand, int64_t start,
                                int64_t count, int64_t silenceFrames = 0);
    void removeMarkerRange(std::vector<DocumentMarker> &, int64_t start,
                           int64_t count);
} // namespace cupuacu::actions::audio
