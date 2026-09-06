#include "RevisionEdit.hpp"
#include "../ViewPolicy.hpp"
#include "../DocumentUi.hpp"
#include "../../concurrency/DeferredRelease.hpp"

namespace cupuacu::actions::audio
{
    RevisionEditState RevisionEditState::capture(const DocumentSession &session)
    {
        return {session.getEditRevision(), session.document.getMarkers(),
                session.selection, session.cursor};
    }

    RevisionEdit::RevisionEdit(State *state, int index, std::string name,
                               RevisionEditState old, RevisionEditState next,
                               std::optional<ClipboardAudio> copied, bool trim)
        : Undoable(state), tabId(state->tabs.at(index).id),
          description(std::move(name)), before(std::move(old)),
          after(std::move(next)), copiedAudio(std::move(copied)),
          trimView(trim), beforeView(state->tabs.at(index).viewState)
    {
        if (!before.audio || !after.audio)
        {
            throw std::invalid_argument(
                "Revision edit requires both audio revisions");
        }
        before.audio = concurrency::releaseOnWorker(std::move(before.audio));
        after.audio = concurrency::releaseOnWorker(std::move(after.audio));
        updateGui = [this, state]
        {
            if (!committed || !target() || !state->getActiveTab() ||
                state->getActiveTab()->id != tabId)
            {
                return;
            }
            if (state->waveforms.size() !=
                std::size_t(target()->document.getChannelCount()))
            {
                refreshBoundDocumentUi(state);
            }
            if (trimView && (!doingRedo || haveAfterView))
            {
                state->getActiveViewState() =
                    doingRedo ? afterView : beforeView;
                updateSampleOffset(state,
                                   state->getActiveViewState().sampleOffset);
                gui::Waveform::setAllWaveformsDirty(state);
                gui::requestMainViewRefresh(state);
            }
            else
            {
                applyDurationChangeViewPolicy(state);
                if (trimView)
                {
                    afterView = state->getActiveViewState();
                    haveAfterView = true;
                }
            }
        };
    }

    DocumentSession *RevisionEdit::target() const
    {
        for (auto &tab : state->tabs)
        {
            if (tab.id == tabId)
            {
                return &tab.session;
            }
        }
        return nullptr;
    }

    void RevisionEdit::apply(bool redo)
    {
        committed = false;
        auto *session = target();
        const auto &expected = redo ? before : after;
        const auto &next = redo ? after : before;
        if (!session || session->getEditRevision() != expected.audio)
        {
            return;
        }
        // Copy changes editor state only; avoid invalidating an unchanged view.
        if (expected.audio != next.audio &&
            !session->commitEditRevision(expected.audio, next.audio,
                                         next.markers))
        {
            return;
        }
        session->selection = next.selection;
        session->cursor = next.cursor;
        session->syncSelectionAndCursorToDocumentLength();
        if (redo && copiedAudio)
        {
            state->clipboard = *copiedAudio;
        }
        doingRedo = redo;
        committed = true;
    }

    void removeMarkerRange(std::vector<DocumentMarker> &markers, int64_t start,
                           int64_t count)
    {
        for (auto &marker : markers)
        {
            if (marker.frame >= start + count)
            {
                marker.frame -= count;
            }
            else if (marker.frame >= start)
            {
                marker.frame = start;
            }
        }
    }

    void performRevisionCommand(
        State *state, RevisionCommand command, int64_t start, int64_t count,
        int64_t silenceFrames,
        std::shared_ptr<const storage::AudioEditRevision> pasteSource)
    {
        auto &session = state->getActiveDocumentSession();
        auto before = RevisionEditState::capture(session);
        auto after = before;
        storage::AudioEditTransaction edit(*before.audio);
        std::optional<ClipboardAudio> copied;
        std::string name;
        if (command == RevisionCommand::Copy || command == RevisionCommand::Cut)
        {
            storage::AudioEditTransaction slice(*before.audio);
            slice.trim(start, count);
            copied.emplace();
            copied->assignRevision(slice.finish());
        }
        switch (command)
        {
            case RevisionCommand::Copy:
                name = "Copy";
                after.selection.setValue1(start);
                after.selection.setValue2(start + count);
                break;
            case RevisionCommand::Cut:
            case RevisionCommand::Delete:
                name = command == RevisionCommand::Cut ? "Cut" : "Delete";
                edit.erase(start, count);
                removeMarkerRange(after.markers, start, count);
                after.selection.reset();
                break;
            case RevisionCommand::Trim:
                name = "Trim";
                edit.trim(start, count);
                for (auto &marker : after.markers)
                {
                    marker.frame =
                        std::clamp(marker.frame - start, int64_t{0}, count);
                }
                after.selection = gui::Selection<double>(0);
                after.selection.setValue1(0);
                after.selection.setValue2(count);
                break;
            case RevisionCommand::Paste:
            case RevisionCommand::InsertSilence:
            {
                name = command == RevisionCommand::Paste ? "Paste"
                                                         : "Insert silence";
                auto inserted = pasteSource
                                    ? pasteSource
                                    : state->clipboard.getAudioRevision();
                if (command == RevisionCommand::InsertSilence)
                {
                    auto shape = before.audio->shape();
                    shape.frames = silenceFrames;
                    inserted = storage::AudioEditRevision::silence(shape);
                }
                if (!inserted)
                {
                    throw std::logic_error(
                        "Paste requires a reference clipboard");
                }
                inserted = inserted->forPaste(before.audio->shape());
                edit.replace(start, count, *inserted);
                removeMarkerRange(after.markers, start, count);
                for (auto &marker : after.markers)
                {
                    if (marker.frame >= start)
                    {
                        marker.frame += inserted->shape().frames;
                    }
                }
                after.selection = gui::Selection<double>(0);
                after.selection.setValue1(start);
                after.selection.setValue2(start + inserted->shape().frames);
                break;
            }
        }
        after.cursor = command == RevisionCommand::Trim ? 0 : start;
        if (command != RevisionCommand::Copy)
        {
            after.audio = edit.finish();
        }
        state->addAndDoUndoable(std::make_shared<RevisionEdit>(
            state, state->activeTabIndex, std::move(name), std::move(before),
            std::move(after), std::move(copied),
            command == RevisionCommand::Trim));
    }
} // namespace cupuacu::actions::audio
