#pragma once
#include "DurationMutationUndoable.hpp"
#include "SegmentStore.hpp"
#include "TransactionalAudioEdit.hpp"
#include "../../Document.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace cupuacu::actions::audio
{
    class Cut : public DurationMutationUndoable
    {
        int64_t startFrame;
        int64_t numFrames;

        undo::UndoStore::SegmentHandle removedHandle;

        double oldSel1 = 0.0;
        double oldSel2 = 0.0;
        int64_t oldCursorPos = 0;
        bool lastCommitted = true;

    public:
        Cut(State *state, int64_t start, int64_t count)
            : DurationMutationUndoable(state), startFrame(start),
              numFrames(count)
        {
            auto &session = state->getActiveDocumentSession();
            if (session.selection.isActive())
            {
                oldSel1 = session.selection.getStart();
                oldSel2 = session.selection.getEnd();
            }

            oldCursorPos = session.cursor;
        }

        Cut(State *state, int64_t start, int64_t count,
            undo::UndoStore::SegmentHandle removedHandleToUse,
            const double oldSel1ToUse, const double oldSel2ToUse,
            const int64_t oldCursorPosToUse)
            : DurationMutationUndoable(state), startFrame(start),
              numFrames(count), removedHandle(std::move(removedHandleToUse)),
              oldSel1(oldSel1ToUse), oldSel2(oldSel2ToUse),
              oldCursorPos(oldCursorPosToUse)
        {
        }

        void redo() override
        {
            lastCommitted = false;
            auto &session = state->getActiveDocumentSession();
            auto &doc = session.document;
            const int64_t total = doc.getFrameCount();

            if (numFrames <= 0 || startFrame < 0 || startFrame >= total)
            {
                return;
            }

            const int64_t actualCount =
                std::min<int64_t>(numFrames, total - startFrame);
            numFrames = actualCount;
            cupuacu::LongTaskScope longTask(
                state, "Cutting audio", "Capturing selection", 0.0, false,
                true);
            cupuacu::renderLongTaskOverlayNow(state);
            detail::OperationProgressUi progressUi(state, "Capturing selection");
            auto previousClipboard = state->clipboard;

            try
            {
                Document::AudioSegment removed = detail::captureOrLoadSegment(
                    session, removedHandle,
                    [&]
                    {
                        return doc.captureSegment(
                            startFrame, numFrames,
                            [&](const int64_t completed, const int64_t totalToCopy)
                            {
                                detail::publishCancelablePhaseProgress(
                                    state, progressUi, "Capturing selection",
                                    0.0, 0.44, completed, totalToCopy);
                            });
                    });
                if (removedHandle.empty())
                {
                    removedHandle = detail::storeSegmentIfNeeded(
                        session, removedHandle, removed, "cut");
                }
                progressUi.publishProgress("Capturing selection", 0.44, true);
                state->clipboard.assignSegment(std::move(removed));

                cupuacu::setLongTask(state, "Cutting audio", "Removing audio",
                                     0.44, false, false);
                progressUi.publishProgress("Removing audio", 0.44, true);
                session.stopWaveformCacheBuild();
                doc.removeFrames(
                    startFrame, numFrames,
                    [&](const int64_t completed, const int64_t totalToRemove)
                    {
                        progressUi.publishPhaseProgress("Removing audio", 0.44,
                                                        0.9, completed,
                                                        totalToRemove);
                    });
                session.syncSelectionAndCursorToDocumentLength();
                updateCursorPos(state, startFrame);
                session.selection.reset();
                detail::rebuildWaveformCacheAfterTransactionalCommit(
                    state, session, progressUi, "Cut complete");
                session.syncSelectionAndCursorToDocumentLength();
                lastCommitted = true;
            }
            catch (const cupuacu::LongTaskCanceledError &)
            {
                state->clipboard = std::move(previousClipboard);
                progressUi.publishProgress("Cut canceled", 0.0, true);
                return;
            }
        }

        void undo() override
        {
            lastCommitted = false;
            auto &session = state->getActiveDocumentSession();
            auto &doc = session.document;
            cupuacu::LongTaskScope longTask(
                state, "Undoing cut", "Capturing retained audio", 0.0, false,
                true);
            cupuacu::renderLongTaskOverlayNow(state);
            detail::OperationProgressUi progressUi(state,
                                                   "Capturing retained audio");

            try
            {
                const auto removed = session.undoStore.readSegment(removedHandle);
                cupuacu::throwIfLongTaskCanceled(state);

                cupuacu::setLongTask(state, "Undoing cut", "Restoring audio", 0.0,
                                     false, false);
                progressUi.publishProgress("Restoring audio", 0.0, true);
                session.stopWaveformCacheBuild();
                doc.insertFrames(
                    startFrame, numFrames,
                    [&](const int64_t completed, const int64_t totalToInsert)
                    {
                        progressUi.publishPhaseProgress("Restoring audio", 0.0,
                                                        0.45, completed,
                                                        totalToInsert);
                    });
                detail::writeSegmentWithCancelableProgress(
                    state, doc, startFrame, removed, progressUi,
                    "Restoring audio", 0.45, 0.9);
                session.syncSelectionAndCursorToDocumentLength();
                if (oldSel1 != 0.0 || oldSel2 != 0.0)
                {
                    session.selection.setValue1(oldSel1);
                    session.selection.setValue2(oldSel2);
                }
                else
                {
                    session.selection.reset();
                }
                updateCursorPos(state, oldCursorPos);
                detail::rebuildWaveformCacheAfterTransactionalCommit(
                    state, session, progressUi, "Undo complete");
                session.syncSelectionAndCursorToDocumentLength();
                lastCommitted = true;
            }
            catch (const cupuacu::LongTaskCanceledError &)
            {
                progressUi.publishProgress("Undo canceled", 0.0, true);
                return;
            }
        }

        std::string getUndoDescription() override
        {
            return "Cut";
        }
        std::string getRedoDescription() override
        {
            return "Cut";
        }

        [[nodiscard]] bool canPersistForRestart() const override
        {
            return !removedHandle.empty();
        }

        [[nodiscard]] std::optional<nlohmann::json>
        serializeForRestart() const override
        {
            if (!canPersistForRestart())
            {
                return std::nullopt;
            }
            return nlohmann::json{
                {"kind", "cut"},
                {"startFrame", startFrame},
                {"frameCount", numFrames},
                {"removedHandle", removedHandle.path.string()},
                {"oldSelectionStart", oldSel1},
                {"oldSelectionEnd", oldSel2},
                {"oldCursorPos", oldCursorPos},
            };
        }

        [[nodiscard]] int64_t getStartFrame() const
        {
            return startFrame;
        }

        [[nodiscard]] int64_t getFrameCount() const
        {
            return numFrames;
        }

        [[nodiscard]] const undo::UndoStore::SegmentHandle &getRemovedHandle() const
        {
            return removedHandle;
        }

        [[nodiscard]] double getOldSelectionStart() const
        {
            return oldSel1;
        }

        [[nodiscard]] double getOldSelectionEnd() const
        {
            return oldSel2;
        }

        [[nodiscard]] int64_t getOldCursorPos() const
        {
            return oldCursorPos;
        }

        [[nodiscard]] cupuacu::file::OverwritePreservationMutation
        overwritePreservationMutation() const override
        {
            return cupuacu::file::OverwritePreservationMutationHelper::compatible();
        }

        [[nodiscard]] bool lastOperationCommitted() const override
        {
            return lastCommitted;
        }
    };

} // namespace cupuacu::actions::audio
