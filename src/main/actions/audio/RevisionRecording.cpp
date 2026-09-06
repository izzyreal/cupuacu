#include "RevisionRecording.hpp"
#include "../../concurrency/DeferredRelease.hpp"
#include "../Play.hpp"
#include "../../gui/MainViewAccess.hpp"
#include "../../gui/Waveform.hpp"

namespace cupuacu::actions
{
    RevisionRecording::RevisionRecording(State *state, int64_t first,
                                         std::filesystem::path directory)
        : tabId(state->getActiveTab()->id), startFrame(first),
          before(audio::RevisionEditState::capture(
              state->getActiveDocumentSession())),
          published(before.audio),
          writer(before.audio, first, std::move(directory))
    {
        writer.startWorker();
    }
    void startRevisionRecording(State *state, int64_t first,
                                std::filesystem::path directory)
    {
        if (state->revisionRecording)
        {
            throw std::logic_error("Recording is still being committed");
        }
        state->revisionRecording =
            concurrency::releaseOnWorker(std::make_shared<RevisionRecording>(
                state, first, std::move(directory)));
    }
    bool pollRevisionRecording(State *state)
    {
        auto recording = state->revisionRecording;
        if (!recording)
        {
            return false;
        }
        const auto result = recording->writer.snapshot();
        int index = -1;
        for (int i = 0; i < int(state->tabs.size()); ++i)
        {
            if (state->tabs[i].id == recording->tabId)
            {
                index = i;
            }
        }
        if (index < 0 || state->tabs[index].session.getEditRevision() !=
                             recording->published)
        {
            if (!recording->discardRemainingInput && state->audioDevices)
            {
                requestStop(state);
            }
            recording->writer.cancel();
            recording->discardRemainingInput = true;
            // Retain the coordinator until the callback queue is drained so
            // stale captured chunks never reach a replacement document.
            if (recording->finishing && result.completed)
            {
                state->revisionRecording.reset();
            }
            return false;
        }
        auto &session = state->tabs[index].session;
        bool changed = false;
        if (result.audio != recording->published)
        {
            changed = session.commitEditRevision(
                recording->published, result.audio, recording->before.markers);
            if (changed)
            {
                recording->published = result.audio;
                session.cursor =
                    std::max(recording->before.cursor, result.endFrame);
                session.syncSelectionAndCursorToDocumentLength();
                if (index == state->activeTabIndex)
                {
                    gui::Waveform::invalidateAllRenderingCaches(state);
                    gui::requestMainViewRefresh(state);
                }
            }
        }
        if (result.completed && !recording->finishing &&
            !recording->discardRemainingInput)
        {
            recording->discardRemainingInput = true;
            if (state->audioDevices)
            {
                requestStop(
                    state); // Includes write failure while capture is live.
            }
        }
        if (result.completed && recording->finishing)
        {
            if (recording->published != recording->before.audio)
            {
                auto after = audio::RevisionEditState::capture(session);
                state->addUndoableToTab(
                    index, std::make_shared<audio::RevisionEdit>(
                               state, index, "Record", recording->before,
                               std::move(after)));
                // Recording preserves shape/format and therefore preservation
                // eligibility. Save validates container limits on its worker;
                // completion must not reopen the reference container here.
            }
            state->revisionRecording.reset();
            if (!result.error.empty())
            {
                const auto message =
                    result.error +
                    " Only successfully written audio was retained.";
                if (state->errorReporter)
                {
                    state->errorReporter("Recording stopped", message);
                }
                else if (SDL_WasInit(SDL_INIT_VIDEO))
                {
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                             "Recording stopped",
                                             message.c_str(), nullptr);
                }
            }
        }
        return changed;
    }
    bool consumeRevisionRecordedAudio(State *state)
    {
        auto recording = state->revisionRecording;
        if (!recording || !state->audioDevices)
        {
            return false;
        }
        cupuacu::audio::RecordedChunk chunk;
        // The UI only copies bounded chunks into the worker queue. File work,
        // peaks and revision assembly do not consume this frame's budget.
        for (int i = 0; i < 64 && state->audioDevices->popRecordedChunk(chunk);
             ++i)
        {
            if (!recording->discardRemainingInput &&
                !recording->writer.submit(chunk))
            {
                recording->discardRemainingInput = true;
                requestStop(state);
            }
        }
        if (!state->audioDevices->hasPendingRecordStart() &&
            !state->audioDevices->isRecording() &&
            !state->audioDevices->hasPendingRecordedAudio())
        {
            recording->finishing = true;
            recording->writer.finish();
        }
        return pollRevisionRecording(state);
    }
} // namespace cupuacu::actions
