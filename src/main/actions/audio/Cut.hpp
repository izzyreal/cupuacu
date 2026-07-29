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
    enum class SelectionRemovalMode
    {
        Cut,
        Delete
    };

    class SelectionRemoval : public DurationMutationUndoable
    {
        int64_t startFrame;
        int64_t numFrames;
        SelectionRemovalMode mode;

        undo::UndoStore::SegmentHandle removedHandle;

        double oldSel1 = 0.0;
        double oldSel2 = 0.0;
        int64_t oldCursorPos = 0;
        bool lastCommitted = true;

        [[nodiscard]] bool copiesToClipboard() const
        {
            return mode == SelectionRemovalMode::Cut;
        }

        [[nodiscard]] std::string operationName() const
        {
            return copiesToClipboard() ? "Cut" : "Delete";
        }

        [[nodiscard]] std::string operationNameLowercase() const
        {
            return copiesToClipboard() ? "cut" : "delete";
        }

    protected:
        SelectionRemoval(State *state, int64_t start, int64_t count,
                         const SelectionRemovalMode modeToUse)
            : DurationMutationUndoable(state), startFrame(start),
              numFrames(count), mode(modeToUse)
        {
            auto &session = state->getActiveDocumentSession();
            if (session.selection.isActive())
            {
                oldSel1 = session.selection.getStart();
                oldSel2 = session.selection.getEnd();
            }

            oldCursorPos = session.cursor;
        }

        SelectionRemoval(
            State *state, int64_t start, int64_t count,
            undo::UndoStore::SegmentHandle removedHandleToUse,
            const double oldSel1ToUse, const double oldSel2ToUse,
            const int64_t oldCursorPosToUse,
            const SelectionRemovalMode modeToUse)
            : DurationMutationUndoable(state), startFrame(start),
              numFrames(count), mode(modeToUse),
              removedHandle(std::move(removedHandleToUse)),
              oldSel1(oldSel1ToUse), oldSel2(oldSel2ToUse),
              oldCursorPos(oldCursorPosToUse)
        {
        }

    public:
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
            const auto name = operationName();
            const auto lowercaseName = operationNameLowercase();
            cupuacu::LongTaskScope longTask(
                state, copiesToClipboard() ? "Cutting audio" : "Deleting audio",
                "Capturing selection", 0.0, false, true);
            cupuacu::renderLongTaskOverlayNow(state);
            detail::OperationProgressUi progressUi(state, "Capturing selection");
            std::optional<ClipboardAudio> previousClipboard;
            if (copiesToClipboard())
            {
                previousClipboard = state->clipboard;
            }

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
                        session, removedHandle, removed, lowercaseName.c_str());
                }
                progressUi.publishProgress("Capturing selection", 0.44, true);
                if (copiesToClipboard())
                {
                    state->clipboard.assignSegment(std::move(removed));
                }
                else
                {
                    removed = {};
                }

                cupuacu::setLongTask(
                    state, copiesToClipboard() ? "Cutting audio"
                                               : "Deleting audio",
                    "Removing audio", 0.44, false, false);
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
                    state, session, progressUi, name + " complete");
                session.syncSelectionAndCursorToDocumentLength();
                lastCommitted = true;
            }
            catch (const cupuacu::LongTaskCanceledError &)
            {
                if (previousClipboard.has_value())
                {
                    state->clipboard = std::move(*previousClipboard);
                }
                progressUi.publishProgress(name + " canceled", 0.0, true);
                return;
            }
        }

        void undo() override
        {
            lastCommitted = false;
            auto &session = state->getActiveDocumentSession();
            auto &doc = session.document;
            const auto undoTitle = "Undoing " + operationNameLowercase();
            cupuacu::LongTaskScope longTask(
                state, undoTitle, "Capturing retained audio", 0.0, false, true);
            cupuacu::renderLongTaskOverlayNow(state);
            detail::OperationProgressUi progressUi(state,
                                                   "Capturing retained audio");

            try
            {
                const auto removed = session.undoStore.readSegment(removedHandle);
                cupuacu::throwIfLongTaskCanceled(state);

                cupuacu::setLongTask(state, undoTitle, "Restoring audio", 0.0,
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
            return operationName();
        }
        std::string getRedoDescription() override
        {
            return operationName();
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
                {"kind", operationNameLowercase()},
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

    class Cut final : public SelectionRemoval
    {
    public:
        Cut(State *state, int64_t start, int64_t count)
            : SelectionRemoval(state, start, count, SelectionRemovalMode::Cut)
        {
        }

        Cut(State *state, int64_t start, int64_t count,
            undo::UndoStore::SegmentHandle removedHandleToUse,
            const double oldSel1ToUse, const double oldSel2ToUse,
            const int64_t oldCursorPosToUse)
            : SelectionRemoval(state, start, count,
                               std::move(removedHandleToUse), oldSel1ToUse,
                               oldSel2ToUse, oldCursorPosToUse,
                               SelectionRemovalMode::Cut)
        {
        }
    };

} // namespace cupuacu::actions::audio
