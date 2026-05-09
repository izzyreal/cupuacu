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
    namespace detail
    {
        inline std::vector<cupuacu::DocumentMarker>
        applyCutMarkerTransform(std::vector<cupuacu::DocumentMarker> markers,
                                const int64_t startFrame,
                                const int64_t numFrames)
        {
            if (numFrames <= 0)
            {
                return markers;
            }

            const int64_t removedEnd = startFrame + numFrames;
            for (auto &marker : markers)
            {
                if (marker.frame >= removedEnd)
                {
                    marker.frame -= numFrames;
                }
                else if (marker.frame >= startFrame)
                {
                    marker.frame = startFrame;
                }
            }
            return markers;
        }

        inline std::vector<cupuacu::DocumentMarker>
        applyUndoCutMarkerTransform(std::vector<cupuacu::DocumentMarker> markers,
                                    const int64_t startFrame,
                                    const int64_t numFrames)
        {
            if (numFrames <= 0)
            {
                return markers;
            }

            for (auto &marker : markers)
            {
                if (marker.frame >= startFrame)
                {
                    marker.frame += numFrames;
                }
            }
            return markers;
        }
    } // namespace detail

    class Cut : public DurationMutationUndoable
    {
        int64_t startFrame;
        int64_t numFrames;

        undo::UndoStore::SegmentHandle removedHandle;

        double oldSel1 = 0.0;
        double oldSel2 = 0.0;
        int64_t oldCursorPos = 0;
        bool lastCommitted = true;

        [[nodiscard]] cupuacu::Document buildCutDocument(
            const cupuacu::Document &sourceDocument,
            const cupuacu::Document::AudioSegment &before,
            const cupuacu::Document::AudioSegment &after,
            std::vector<cupuacu::DocumentMarker> markers,
            detail::OperationProgressUi &progressUi) const
        {
            cupuacu::Document replacement;
            replacement.initialize(sourceDocument.getSampleFormat(),
                                   sourceDocument.getSampleRate(),
                                   static_cast<uint32_t>(
                                       sourceDocument.getChannelCount()),
                                   sourceDocument.getFrameCount() - numFrames);
            detail::writeSegmentWithCancelableProgress(
                state, replacement, 0, before, progressUi,
                "Preparing cut result", 0.8, 0.85);
            detail::writeSegmentWithCancelableProgress(
                state, replacement, startFrame, after, progressUi,
                "Preparing cut result", 0.85, 0.9);
            replacement.replaceMarkers(std::move(markers));
            replacement.adoptPreservationSourceId(
                sourceDocument.getPreservationSourceId());
            return replacement;
        }

        [[nodiscard]] cupuacu::Document buildUndoDocument(
            const cupuacu::Document &sourceDocument,
            const cupuacu::Document::AudioSegment &before,
            const cupuacu::Document::AudioSegment &removed,
            const cupuacu::Document::AudioSegment &after,
            std::vector<cupuacu::DocumentMarker> markers,
            detail::OperationProgressUi &progressUi) const
        {
            cupuacu::Document replacement;
            replacement.initialize(sourceDocument.getSampleFormat(),
                                   sourceDocument.getSampleRate(),
                                   static_cast<uint32_t>(
                                       sourceDocument.getChannelCount()),
                                   sourceDocument.getFrameCount() + numFrames);
            detail::writeSegmentWithCancelableProgress(
                state, replacement, 0, before, progressUi,
                "Preparing restored document", 0.6, 0.7);
            detail::writeSegmentWithCancelableProgress(
                state, replacement, startFrame, removed, progressUi,
                "Preparing restored document", 0.7, 0.8);
            detail::writeSegmentWithCancelableProgress(
                state, replacement, startFrame + numFrames, after, progressUi,
                "Preparing restored document", 0.8, 0.9);
            replacement.replaceMarkers(std::move(markers));
            replacement.adoptPreservationSourceId(
                sourceDocument.getPreservationSourceId());
            return replacement;
        }

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
                const int64_t tailCount = total - (startFrame + numFrames);
                auto markers = [&]()
                {
                    auto lease = doc.acquireReadLease();
                    return detail::applyCutMarkerTransform(
                        std::vector<cupuacu::DocumentMarker>(
                            lease.getMarkers().begin(), lease.getMarkers().end()),
                        startFrame, numFrames);
                }();

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
                                    0.0, 0.22, completed, totalToCopy);
                            });
                    });
                if (removedHandle.empty())
                {
                    removedHandle = detail::storeSegmentIfNeeded(
                        session, removedHandle, removed, "cut");
                }
                progressUi.publishProgress("Capturing selection", 0.22, true);

                progressUi.publishProgress("Writing clipboard", 0.22, true);
                state->clipboard.assignSegment(
                    removed,
                    [&](const int64_t completed, const int64_t totalToCopy)
                    {
                        detail::publishCancelablePhaseProgress(
                            state, progressUi, "Writing clipboard", 0.22, 0.44,
                            completed, totalToCopy);
                    });
                cupuacu::throwIfLongTaskCanceled(state);

                progressUi.publishProgress("Capturing kept audio", 0.44, true);
                const auto before = doc.captureSegment(
                    0, startFrame,
                    [&](const int64_t completed, const int64_t totalToCopy)
                    {
                        detail::publishCancelablePhaseProgress(
                            state, progressUi, "Capturing kept audio", 0.44, 0.62,
                            completed, totalToCopy);
                    });
                const auto after = doc.captureSegment(
                    startFrame + numFrames, tailCount,
                    [&](const int64_t completed, const int64_t totalToCopy)
                    {
                        detail::publishCancelablePhaseProgress(
                            state, progressUi, "Capturing kept audio", 0.62, 0.8,
                            completed, totalToCopy);
                    });

                auto replacement = buildCutDocument(doc, before, after,
                                                    std::move(markers), progressUi);
                cupuacu::throwIfLongTaskCanceled(state);

                cupuacu::setLongTask(state, "Cutting audio", "Committing cut",
                                     0.9, false, false);
                progressUi.publishProgress("Committing cut", 0.9, true);
                session.stopWaveformCacheBuild();
                session.document = std::move(replacement);
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
                const auto markers = [&]()
                {
                    auto lease = doc.acquireReadLease();
                    return detail::applyUndoCutMarkerTransform(
                        std::vector<cupuacu::DocumentMarker>(
                            lease.getMarkers().begin(), lease.getMarkers().end()),
                        startFrame, numFrames);
                }();
                const auto removed = session.undoStore.readSegment(removedHandle);
                cupuacu::throwIfLongTaskCanceled(state);

                const auto before = doc.captureSegment(
                    0, startFrame,
                    [&](const int64_t completed, const int64_t totalToCopy)
                    {
                        detail::publishCancelablePhaseProgress(
                            state, progressUi, "Capturing retained audio", 0.0,
                            0.3, completed, totalToCopy);
                    });
                const auto after = doc.captureSegment(
                    startFrame, std::max<int64_t>(0, doc.getFrameCount() - startFrame),
                    [&](const int64_t completed, const int64_t totalToCopy)
                    {
                        detail::publishCancelablePhaseProgress(
                            state, progressUi, "Capturing retained audio", 0.3,
                            0.6, completed, totalToCopy);
                    });

                auto replacement = buildUndoDocument(doc, before, removed, after,
                                                     std::move(markers), progressUi);
                cupuacu::throwIfLongTaskCanceled(state);

                cupuacu::setLongTask(state, "Undoing cut", "Committing undo",
                                     0.9, false, false);
                progressUi.publishProgress("Committing undo", 0.9, true);
                session.stopWaveformCacheBuild();
                session.document = std::move(replacement);
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
