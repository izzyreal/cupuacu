#pragma once
#include "DurationMutationUndoable.hpp"
#include "SegmentStore.hpp"
#include "TransactionalAudioEdit.hpp"
#include "../../Document.hpp"
#include <algorithm>
#include <optional>
#include <vector>
#include <cstdint>

namespace cupuacu::actions::audio
{

    class Paste : public DurationMutationUndoable
    {
        int64_t startFrame;
        int64_t endFrame;

        int64_t insertedFrameCount = 0;
        int64_t overwrittenFrameCount = 0;

        undo::UndoStore::SegmentHandle insertedHandle;
        undo::UndoStore::SegmentHandle overwrittenHandle;

        bool hadOldSelection = false;
        double oldSel1 = 0;
        double oldSel2 = 0;
        int64_t oldCursorPos = 0;
        bool lastCommitted = true;

        [[nodiscard]] static std::vector<cupuacu::DocumentMarker>
        applyPasteRedoMarkerTransform(
            std::vector<cupuacu::DocumentMarker> markers,
            const int64_t startFrame, const int64_t overwrittenFrameCount,
            const int64_t insertedFrameCount)
        {
            const int64_t overwrittenEnd = startFrame + overwrittenFrameCount;
            for (auto &marker : markers)
            {
                if (overwrittenFrameCount > 0)
                {
                    if (marker.frame >= overwrittenEnd)
                    {
                        marker.frame -= overwrittenFrameCount;
                    }
                    else if (marker.frame >= startFrame)
                    {
                        marker.frame = startFrame;
                    }
                }
                if (marker.frame >= startFrame)
                {
                    marker.frame += insertedFrameCount;
                }
            }
            return markers;
        }

        [[nodiscard]] static std::vector<cupuacu::DocumentMarker>
        applyPasteUndoMarkerTransform(
            std::vector<cupuacu::DocumentMarker> markers,
            const int64_t startFrame, const int64_t insertedFrameCount,
            const int64_t overwrittenFrameCount)
        {
            const int64_t insertedEnd = startFrame + insertedFrameCount;
            for (auto &marker : markers)
            {
                if (marker.frame >= insertedEnd)
                {
                    marker.frame -= insertedFrameCount;
                }
                else if (marker.frame >= startFrame)
                {
                    marker.frame = startFrame;
                }
                if (overwrittenFrameCount > 0 && marker.frame >= startFrame)
                {
                    marker.frame += overwrittenFrameCount;
                }
            }
            return markers;
        }

        [[nodiscard]] cupuacu::Document buildRedoDocument(
            const cupuacu::Document &sourceDocument,
            const cupuacu::Document::AudioSegment &before,
            const cupuacu::Document::AudioSegment &inserted,
            const cupuacu::Document::AudioSegment &after,
            std::vector<cupuacu::DocumentMarker> markers,
            detail::OperationProgressUi &progressUi,
            const int64_t outputFrameCount) const
        {
            cupuacu::Document replacement;
            replacement.initialize(sourceDocument.getSampleFormat(),
                                   sourceDocument.getSampleRate(),
                                   static_cast<uint32_t>(
                                       sourceDocument.getChannelCount()),
                                   outputFrameCount);
            detail::writeSegmentWithCancelableProgress(
                state, replacement, 0, before, progressUi,
                "Preparing pasted document", 0.72, 0.78);
            detail::writeSegmentWithCancelableProgress(
                state, replacement, startFrame, inserted, progressUi,
                "Preparing pasted document", 0.78, 0.84);
            detail::writeSegmentWithCancelableProgress(
                state, replacement, startFrame + insertedFrameCount, after,
                progressUi, "Preparing pasted document", 0.84, 0.9);
            replacement.replaceMarkers(std::move(markers));
            replacement.adoptPreservationSourceId(
                sourceDocument.getPreservationSourceId());
            return replacement;
        }

        [[nodiscard]] cupuacu::Document buildUndoDocument(
            const cupuacu::Document &sourceDocument,
            const cupuacu::Document::AudioSegment &before,
            const std::optional<cupuacu::Document::AudioSegment> &overwritten,
            const cupuacu::Document::AudioSegment &after,
            std::vector<cupuacu::DocumentMarker> markers,
            detail::OperationProgressUi &progressUi,
            const int64_t outputFrameCount) const
        {
            cupuacu::Document replacement;
            replacement.initialize(sourceDocument.getSampleFormat(),
                                   sourceDocument.getSampleRate(),
                                   static_cast<uint32_t>(
                                       sourceDocument.getChannelCount()),
                                   outputFrameCount);
            detail::writeSegmentWithCancelableProgress(
                state, replacement, 0, before, progressUi,
                "Preparing restored document", 0.68, 0.76);
            if (overwritten.has_value())
            {
                detail::writeSegmentWithCancelableProgress(
                    state, replacement, startFrame, *overwritten, progressUi,
                    "Preparing restored document", 0.76, 0.84);
            }
            detail::writeSegmentWithCancelableProgress(
                state, replacement,
                startFrame + (overwritten.has_value() ? overwrittenFrameCount : 0),
                after, progressUi, "Preparing restored document", 0.84, 0.9);
            replacement.replaceMarkers(std::move(markers));
            replacement.adoptPreservationSourceId(
                sourceDocument.getPreservationSourceId());
            return replacement;
        }

    public:
        Paste(State *state, int64_t start, int64_t end = -1)
            : DurationMutationUndoable(state), startFrame(start),
              endFrame(end)
        {
            auto &session = state->getActiveDocumentSession();
            if (session.selection.isActive())
            {
                hadOldSelection = true;
                oldSel1 = session.selection.getStart();
                oldSel2 = session.selection.getEnd();
            }
            else if (endFrame >= startFrame && endFrame >= 0)
            {
                hadOldSelection = true;
                oldSel1 = static_cast<double>(startFrame);
                oldSel2 = static_cast<double>(endFrame);
            }

            oldCursorPos = session.cursor;
        }

        Paste(State *state, const int64_t start, const int64_t end,
              const int64_t insertedFrameCountToUse,
              const int64_t overwrittenFrameCountToUse,
              undo::UndoStore::SegmentHandle insertedHandleToUse,
              undo::UndoStore::SegmentHandle overwrittenHandleToUse,
              const bool hadOldSelectionToUse, const double oldSel1ToUse,
              const double oldSel2ToUse, const int64_t oldCursorPosToUse)
            : DurationMutationUndoable(state), startFrame(start), endFrame(end),
              insertedFrameCount(insertedFrameCountToUse),
              overwrittenFrameCount(overwrittenFrameCountToUse),
              insertedHandle(std::move(insertedHandleToUse)),
              overwrittenHandle(std::move(overwrittenHandleToUse)),
              hadOldSelection(hadOldSelectionToUse), oldSel1(oldSel1ToUse),
              oldSel2(oldSel2ToUse), oldCursorPos(oldCursorPosToUse)
        {
        }

        void redo() override
        {
            lastCommitted = false;
            auto &session = state->getActiveDocumentSession();
            const auto &clip = state->clipboard;
            auto &doc = session.document;
            const int64_t docFrames = doc.getFrameCount();

            if (startFrame < 0 || startFrame > docFrames)
            {
                return;
            }

            if (insertedHandle.empty() && clip.getFrameCount() == 0)
            {
                return;
            }

            cupuacu::LongTaskScope longTask(
                state, getRedoDescription(), "Capturing inserted audio", 0.0, false,
                true);
            cupuacu::renderLongTaskOverlayNow(state);
            detail::OperationProgressUi progressUi(state,
                                                   "Capturing inserted audio");

            try
            {
                const cupuacu::Document::AudioSegment *inserted = nullptr;
                std::optional<cupuacu::Document::AudioSegment> insertedOwned;
                if (insertedHandle.empty())
                {
                    insertedFrameCount = clip.getFrameCount();
                }

                if (!insertedHandle.empty())
                {
                    insertedOwned = session.undoStore.readSegment(insertedHandle);
                    inserted = &*insertedOwned;
                }
                else if (const auto *clipboardSegment =
                             clip.getWholeSegmentIfFrameCountMatches(
                                 insertedFrameCount))
                {
                    progressUi.publishProgress("Capturing inserted audio", 0.24,
                                               true);
                    inserted = clipboardSegment;
                }
                else
                {
                    insertedOwned = clip.captureSegment(
                        0, insertedFrameCount,
                        [&](const int64_t completed, const int64_t totalToCopy)
                        {
                            detail::publishCancelablePhaseProgress(
                                state, progressUi, "Capturing inserted audio",
                                0.0, 0.24, completed, totalToCopy);
                        });
                    inserted = &*insertedOwned;
                }

                insertedHandle = detail::storeSegmentIfNeeded(
                    session, insertedHandle, *inserted, "paste-inserted");
                insertedFrameCount = inserted->frameCount;
                overwrittenFrameCount = 0;

                std::optional<cupuacu::Document::AudioSegment> overwritten;
                if (endFrame >= 0 && endFrame > startFrame)
                {
                    overwrittenFrameCount =
                        std::min(endFrame - startFrame, docFrames - startFrame);
                    overwrittenFrameCount =
                        std::max<int64_t>(0, overwrittenFrameCount);

                    if (overwrittenFrameCount > 0)
                    {
                        overwritten = detail::captureOrLoadSegment(
                            session, overwrittenHandle,
                            [&]
                            {
                                return doc.captureSegment(
                                    startFrame, overwrittenFrameCount,
                                    [&](const int64_t completed,
                                        const int64_t totalToCopy)
                                    {
                                        detail::publishCancelablePhaseProgress(
                                            state, progressUi,
                                            "Capturing overwritten audio", 0.24,
                                            0.46, completed, totalToCopy);
                                    });
                            });
                        overwrittenHandle = detail::storeSegmentIfNeeded(
                            session, overwrittenHandle, *overwritten,
                            "paste-overwritten");
                    }
                }

                progressUi.publishProgress("Capturing surrounding audio", 0.46,
                                           true);
                const auto before = doc.captureSegment(
                    0, startFrame,
                    [&](const int64_t completed, const int64_t totalToCopy)
                    {
                        detail::publishCancelablePhaseProgress(
                            state, progressUi, "Capturing surrounding audio", 0.46,
                            0.59, completed, totalToCopy);
                    });
                const auto after = doc.captureSegment(
                    startFrame + overwrittenFrameCount,
                    docFrames - (startFrame + overwrittenFrameCount),
                    [&](const int64_t completed, const int64_t totalToCopy)
                    {
                        detail::publishCancelablePhaseProgress(
                            state, progressUi, "Capturing surrounding audio", 0.59,
                            0.72, completed, totalToCopy);
                    });

                auto markers = [&]()
                {
                    auto lease = doc.acquireReadLease();
                    return applyPasteRedoMarkerTransform(
                        std::vector<cupuacu::DocumentMarker>(
                            lease.getMarkers().begin(), lease.getMarkers().end()),
                        startFrame, overwrittenFrameCount, insertedFrameCount);
                }();

                auto replacement = buildRedoDocument(
                    doc, before, *inserted, after, std::move(markers), progressUi,
                    docFrames - overwrittenFrameCount + insertedFrameCount);
                cupuacu::throwIfLongTaskCanceled(state);

                cupuacu::setLongTask(state, getRedoDescription(),
                                     "Committing paste", 0.9, false, false);
                progressUi.publishProgress("Committing paste", 0.9, true);
                session.stopWaveformCacheBuild();
                session.document = std::move(replacement);
                session.syncSelectionAndCursorToDocumentLength();
                session.selection.setValue1(startFrame);
                session.selection.setValue2(startFrame + insertedFrameCount);
                updateCursorPos(state, startFrame);
                detail::rebuildWaveformCacheAfterTransactionalCommit(
                    state, session, progressUi, "Paste complete");
                session.syncSelectionAndCursorToDocumentLength();
                lastCommitted = true;
            }
            catch (const cupuacu::LongTaskCanceledError &)
            {
                progressUi.publishProgress("Paste canceled", 0.0, true);
                return;
            }
        }

        void undo() override
        {
            lastCommitted = false;
            auto &session = state->getActiveDocumentSession();
            auto &doc = session.document;
            const int64_t docFrames = doc.getFrameCount();

            if (startFrame < 0 || startFrame >= docFrames)
            {
                return;
            }

            cupuacu::LongTaskScope longTask(
                state, std::string("Undoing ") + getUndoDescription(),
                "Capturing surrounding audio", 0.0, false, true);
            cupuacu::renderLongTaskOverlayNow(state);
            detail::OperationProgressUi progressUi(state,
                                                   "Capturing surrounding audio");

            try
            {
                const int64_t removeCount =
                    std::min<int64_t>(insertedFrameCount, docFrames - startFrame);
                const auto before = doc.captureSegment(
                    0, startFrame,
                    [&](const int64_t completed, const int64_t totalToCopy)
                    {
                        detail::publishCancelablePhaseProgress(
                            state, progressUi, "Capturing surrounding audio", 0.0,
                            0.3, completed, totalToCopy);
                    });
                const auto after = doc.captureSegment(
                    startFrame + removeCount, docFrames - (startFrame + removeCount),
                    [&](const int64_t completed, const int64_t totalToCopy)
                    {
                        detail::publishCancelablePhaseProgress(
                            state, progressUi, "Capturing surrounding audio", 0.3,
                            0.6, completed, totalToCopy);
                    });

                std::optional<cupuacu::Document::AudioSegment> overwritten;
                if (endFrame >= 0 && overwrittenFrameCount > 0)
                {
                    overwritten = session.undoStore.readSegment(overwrittenHandle);
                }
                cupuacu::throwIfLongTaskCanceled(state);

                auto markers = [&]()
                {
                    auto lease = doc.acquireReadLease();
                    return applyPasteUndoMarkerTransform(
                        std::vector<cupuacu::DocumentMarker>(
                            lease.getMarkers().begin(), lease.getMarkers().end()),
                        startFrame, removeCount, overwrittenFrameCount);
                }();

                auto replacement = buildUndoDocument(
                    doc, before, overwritten, after, std::move(markers), progressUi,
                    docFrames - removeCount + overwrittenFrameCount);
                cupuacu::throwIfLongTaskCanceled(state);

                cupuacu::setLongTask(state,
                                     std::string("Undoing ") + getUndoDescription(),
                                     "Committing undo", 0.9, false, false);
                progressUi.publishProgress("Committing undo", 0.9, true);
                session.stopWaveformCacheBuild();
                session.document = std::move(replacement);
                session.syncSelectionAndCursorToDocumentLength();
                if (hadOldSelection)
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
            return (endFrame >= 0 ? "Paste overwrite" : "Paste insert");
        }

        std::string getRedoDescription() override
        {
            return getUndoDescription();
        }

        [[nodiscard]] bool canPersistForRestart() const override
        {
            return !insertedHandle.empty() &&
                   (overwrittenFrameCount <= 0 || !overwrittenHandle.empty());
        }

        [[nodiscard]] std::optional<nlohmann::json>
        serializeForRestart() const override
        {
            if (!canPersistForRestart())
            {
                return std::nullopt;
            }
            return nlohmann::json{
                {"kind", "paste"},
                {"startFrame", startFrame},
                {"endFrame", endFrame},
                {"insertedFrameCount", insertedFrameCount},
                {"overwrittenFrameCount", overwrittenFrameCount},
                {"insertedHandle", insertedHandle.path.string()},
                {"overwrittenHandle", overwrittenHandle.path.string()},
                {"hadOldSelection", hadOldSelection},
                {"oldSelectionStart", oldSel1},
                {"oldSelectionEnd", oldSel2},
                {"oldCursorPos", oldCursorPos},
            };
        }

        [[nodiscard]] int64_t getStartFrame() const
        {
            return startFrame;
        }

        [[nodiscard]] int64_t getEndFrame() const
        {
            return endFrame;
        }

        [[nodiscard]] int64_t getInsertedFrameCount() const
        {
            return insertedFrameCount;
        }

        [[nodiscard]] int64_t getOverwrittenFrameCount() const
        {
            return overwrittenFrameCount;
        }

        [[nodiscard]] const undo::UndoStore::SegmentHandle &getInsertedHandle() const
        {
            return insertedHandle;
        }

        [[nodiscard]] const undo::UndoStore::SegmentHandle &
        getOverwrittenHandle() const
        {
            return overwrittenHandle;
        }

        [[nodiscard]] bool getHadOldSelection() const
        {
            return hadOldSelection;
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
