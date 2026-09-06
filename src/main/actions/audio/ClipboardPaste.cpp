#include "ClipboardPaste.hpp"
#include "EditCommands.hpp"
#include "../DocumentOperationAccess.hpp"
#include "../../storage/ClipboardConversion.hpp"
#include "../../LongTask.hpp"
#include "../../concurrency/DeferredRelease.hpp"

namespace cupuacu::actions::audio
{
    void prepareEmptyRevisionPaste(State *state)
    {
        auto &session = state->getActiveDocumentSession();
        auto &document = session.document;
        const auto &clipboard = state->clipboard.getAudioRevision();
        if (!clipboard || session.hasReadRevision() ||
            document.getFrameCount() != 0 ||
            !state->getActiveUndoables().empty() ||
            !state->getActiveRedoables().empty())
        {
            return;
        }

        // A fresh tab has no format yet. Adopt the clipboard's format; an
        // explicitly configured empty document keeps its own. Establish an
        // empty saved revision so paste and its history only share references.
        storage::AudioShape shape{0, int(document.getChannelCount()),
                                  document.getSampleRate(),
                                  document.getSampleFormat()};
        if (shape.channels == 0)
        {
            shape = clipboard->shape();
            shape.frames = 0;
        }
        auto empty = storage::AudioEditRevision::silence(shape);
        document.setExternalAudioShape(shape.format, shape.sampleRate,
                                       shape.channels, 0);
        session.bindReadRevision(std::move(empty));
    }

    void beginClipboardPaste(State *state, int64_t start, int64_t end)
    {
        if (state->backgroundClipboardConversion)
        {
            return;
        }
        auto &session = state->getActiveDocumentSession();
        auto path =
            (state->paths ? state->paths->statePath()
                          : std::filesystem::temp_directory_path()) /
            ("clipboard-conversion-" +
             std::to_string(
                 std::chrono::steady_clock::now().time_since_epoch().count()));
        auto job = std::make_shared<storage::ClipboardConversion>(
            state->clipboard, session.hasReadRevision(), path,
            state->taskScheduler);
        static uint64_t nextOperation = 1;
        job->operationId = nextOperation++;
        job->tabId = state->getActiveTab()->id;
        job->documentVersion = session.document.getWaveformDataVersion();
        job->start = start;
        job->end = end;
        job->cursor = session.cursor;
        job->selected = session.selection.isActive();
        state->backgroundClipboardConversion = std::move(job);
        state->getActiveTab()->operation =
            DocumentOperation{DocumentOperation::Kind::Edit,
                              state->backgroundClipboardConversion->operationId,
                              "Pasting audio",
                              "Preparing clipboard",
                              {},
                              false};
    }

    void processPendingClipboardPaste(State *state)
    {
        auto job = state->backgroundClipboardConversion;
        if (!job)
        {
            return;
        }
        if (operationCanceled(state, job->tabId, DocumentOperation::Kind::Edit,
                              job->operationId) ||
            state->quitRequestedAfterLongTaskCancel)
        {
            state->backgroundClipboardConversion.reset();
            job->close();
            finishOperation(state, job->tabId, DocumentOperation::Kind::Edit,
                            job->operationId);
            return;
        }
        auto result = job->takePublished();
        if (!result)
        {
            return;
        }
        state->backgroundClipboardConversion.reset();
        job->close();
        finishOperation(state, job->tabId, DocumentOperation::Kind::Edit,
                        job->operationId);
        try
        {
            if (result->error)
            {
                std::rethrow_exception(result->error);
            }
            if (!result->value)
            {
                return;
            }
            auto converted = std::move(*result->value);
            auto *tab = findOperationTab(state, job->tabId);
            if (!tab)
            {
                throw std::runtime_error("Paste target was closed");
            }
            auto &session = tab->session;
            if (session.document.getWaveformDataVersion() !=
                job->documentVersion)
            {
                throw std::runtime_error(
                    "Paste target changed while preparing clipboard");
            }
            if (session.hasReadRevision())
            {
                performRevisionCommand(
                    state, RevisionCommand::Paste, job->start,
                    job->end < 0 ? 0 : job->end - job->start, 0,
                    converted->getAudioRevision(), job->tabId);
            }
            else
            {
                if (state->getActiveTab()->id != job->tabId)
                {
                    throw std::runtime_error("Paste target changed");
                }
                state->addAndDoUndoable(std::make_shared<Paste>(
                    state, job->start, job->end, converted));
            }
        }
        catch (const std::exception &error)
        {
            if (state->errorReporter)
            {
                state->errorReporter("Paste", error.what());
            }
            else
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Paste: %s",
                             error.what());
            }
        }
    }
} // namespace cupuacu::actions::audio
