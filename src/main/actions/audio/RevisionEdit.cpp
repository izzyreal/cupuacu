#include "RevisionEdit.hpp"
#include "../ViewPolicy.hpp"
#include "../DocumentOperationAccess.hpp"
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

    void removeMarkerRange(std::span<DocumentMarker> markers, int64_t start,
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

    struct PreparedRevisionCommand
    {
        RevisionEditState before, after;
        std::optional<ClipboardAudio> copied;
        std::string name;
        bool trim = false;
        std::function<void(State *, int)> publish;
    };
} // namespace cupuacu::actions::audio
namespace cupuacu::concurrency
{
    struct RevisionCommandJob
    {
        uint64_t tabId = 0, id = 0, documentVersion = 0;
        std::shared_ptr<std::atomic_bool> canceled =
            std::make_shared<std::atomic_bool>(false);
        std::future<actions::audio::PreparedRevisionCommand> result;
        ~RevisionCommandJob()
        {
            canceled->store(true);
        }
    };
} // namespace cupuacu::concurrency
namespace cupuacu::actions::audio
{
    using PendingRevisionCommand = concurrency::RevisionCommandJob;
    static PreparedRevisionCommand prepareRevisionCommand(
        RevisionEditState before, RevisionCommand command, int64_t start,
        int64_t count, int64_t silenceFrames,
        std::shared_ptr<const storage::AudioEditRevision> pasteSource)
    {
        auto after = before;
        auto base = before.audio;
        if (command == RevisionCommand::Paste && base->shape().channels == 0 &&
            pasteSource)
        {
            auto shape = pasteSource->shape();
            shape.frames = 0;
            base = storage::AudioEditRevision::silence(shape);
        }
        storage::AudioEditTransaction edit(*base);
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
                auto inserted = pasteSource;
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
                inserted = inserted->forPaste(base->shape());
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
            after.audio = concurrency::releaseOnWorker(edit.finish());
        }
        return {std::move(before), std::move(after), std::move(copied),
                std::move(name), command == RevisionCommand::Trim};
    }
    static void queueRevisionCommand(
        State *state,
        std::function<PreparedRevisionCommand(RevisionEditState)> prepare,
        uint64_t targetTabId = 0)
    {
        auto *tab = targetTabId ? findOperationTab(state, targetTabId)
                                : state->getActiveTab();
        if (!tab || (tab->operation && tab->operation->blocksMutation()))
        {
            return;
        }
        static std::atomic<uint64_t> sequence{1};
        auto job = std::make_shared<PendingRevisionCommand>();
        job->tabId = tab->id;
        job->documentVersion = tab->session.document.getWaveformDataVersion();
        job->id = sequence.fetch_add(1);
        auto promise =
            std::make_shared<std::promise<PreparedRevisionCommand>>();
        job->result = promise->get_future();
        auto before = RevisionEditState::capture(tab->session);
        state->revisionCommands.push_back(job);
        try
        {
            state->taskScheduler->submit(
                [promise, cancel = job->canceled, before = std::move(before),
                 prepare = std::move(prepare)]() mutable
                {
                    try
                    {
                        if (cancel->load())
                        {
                            throw std::runtime_error("Edit canceled");
                        }
                        auto prepared = prepare(std::move(before));
                        if (cancel->load())
                        {
                            throw std::runtime_error("Edit canceled");
                        }
                        promise->set_value(std::move(prepared));
                    }
                    catch (...)
                    {
                        promise->set_exception(std::current_exception());
                    }
                },
                {.documentId = job->tabId, .mutation = true});
            tab->operation = DocumentOperation{DocumentOperation::Kind::Edit,
                                               job->id,
                                               "Editing audio",
                                               {},
                                               {},
                                               false};
        }
        catch (...)
        {
            state->revisionCommands.pop_back();
            throw;
        }
    }
    void performRevisionCommand(
        State *state, RevisionCommand command, int64_t start, int64_t count,
        int64_t silenceFrames,
        std::shared_ptr<const storage::AudioEditRevision> pasteSource,
        uint64_t targetTabId)
    {
        if (!pasteSource)
        {
            pasteSource = state->clipboard.getAudioRevision();
        }
        queueRevisionCommand(
            state,
            [=](RevisionEditState before)
            {
                return prepareRevisionCommand(std::move(before), command, start,
                                              count, silenceFrames,
                                              pasteSource);
            },
            targetTabId);
    }
    void prepareRevisionEdit(
        State *state, std::string name,
        std::function<RevisionEditState(const RevisionEditState &)> prepare)
    {
        queueRevisionCommand(
            state,
            [name = std::move(name),
             prepare = std::move(prepare)](RevisionEditState before)
            {
                auto after = prepare(before);
                after.audio =
                    concurrency::releaseOnWorker(std::move(after.audio));
                return PreparedRevisionCommand{
                    std::move(before), std::move(after), {}, name, false};
            });
    }
    void prepareRevisionAction(State *state,
                               std::function<std::function<void(State *, int)>(
                                   const RevisionEditState &)>
                                   prepare)
    {
        queueRevisionCommand(
            state,
            [prepare = std::move(prepare)](RevisionEditState before)
            {
                auto publish = prepare(before);
                return PreparedRevisionCommand{
                    before, before, {}, {}, false, std::move(publish)};
            });
    }
    void processPendingRevisionCommands(State *state)
    {
        for (auto it = state->revisionCommands.begin();
             it != state->revisionCommands.end();)
        {
            auto job = *it;
            const bool canceled = operationCanceled(
                state, job->tabId, DocumentOperation::Kind::Edit, job->id);
            if (canceled)
            {
                job->canceled->store(true);
            }
            if (job->result.wait_for(std::chrono::seconds(0)) !=
                std::future_status::ready)
            {
                ++it;
                continue;
            }
            it = state->revisionCommands.erase(it);
            finishOperation(state, job->tabId, DocumentOperation::Kind::Edit,
                            job->id);
            try
            {
                auto result = job->result.get();
                auto *tab = findOperationTab(state, job->tabId);
                if (canceled || !tab ||
                    tab->session.document.getWaveformDataVersion() !=
                        job->documentVersion ||
                    tab->session.getEditRevision() != result.before.audio)
                {
                    continue;
                }
                const int index = int(tab - state->tabs.data());
                if (result.publish)
                {
                    result.publish(state, index);
                    continue;
                }
                state->addAndDoUndoableToTab(
                    index,
                    std::make_shared<RevisionEdit>(
                        state, index, std::move(result.name),
                        std::move(result.before), std::move(result.after),
                        std::move(result.copied), result.trim));
            }
            catch (const std::exception &error)
            {
                if (!canceled && state->errorReporter)
                {
                    state->errorReporter("Edit", error.what());
                }
                else if (!canceled)
                {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Edit: %s",
                                 error.what());
                }
            }
        }
    }
} // namespace cupuacu::actions::audio
