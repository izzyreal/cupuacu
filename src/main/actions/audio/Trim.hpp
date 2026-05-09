#pragma once
#include "DurationMutationUndoable.hpp"
#include "SegmentStore.hpp"
#include "TransactionalAudioEdit.hpp"
#include "../ViewPolicy.hpp"
#include "../../Document.hpp"
#include "../../gui/MainViewAccess.hpp"
#include "../../gui/Waveform.hpp"
#include <vector>
#include <cstdint>
#include <algorithm>
#include <optional>

namespace cupuacu::actions::audio
{

    class Trim : public DurationMutationUndoable
    {
    public:
        struct ViewSnapshot
        {
            double samplesPerPixel = 1.0;
            double verticalZoom = 1.0;
            int64_t sampleOffset = 0;
        };

    private:
        enum class PendingViewRestore
        {
            None,
            CapturePostRedo,
            RestorePreUndo,
            RestorePostRedo
        };

        int64_t startFrame;
        int64_t length;

        int sampleRate = 0;
        SampleFormat format = SampleFormat::Unknown;

        int64_t oldTotal = 0;
        int64_t beforeCount = 0;
        int64_t middleCount = 0;
        int64_t afterCount = 0;

        undo::UndoStore::SegmentHandle beforeHandle;
        undo::UndoStore::SegmentHandle afterHandle;

        ViewSnapshot preTrimView{};
        ViewSnapshot postTrimView{};
        bool hasPostTrimView = false;
        PendingViewRestore pendingViewRestore = PendingViewRestore::None;
        bool lastCommitted = true;

        static ViewSnapshot captureViewSnapshot(const State *state)
        {
            ViewSnapshot snapshot{};
            if (!state || !state->mainDocumentSessionWindow)
            {
                return snapshot;
            }

            const auto &viewState = state->getActiveViewState();
            snapshot.samplesPerPixel = viewState.samplesPerPixel;
            snapshot.verticalZoom = viewState.verticalZoom;
            snapshot.sampleOffset = viewState.sampleOffset;
            return snapshot;
        }

        void restoreViewSnapshot(const ViewSnapshot &snapshot) const
        {
            if (!state || !state->mainDocumentSessionWindow)
            {
                return;
            }

            auto &viewState = state->getActiveViewState();
            viewState.samplesPerPixel = snapshot.samplesPerPixel;
            viewState.verticalZoom = snapshot.verticalZoom;
            updateSampleOffset(state, snapshot.sampleOffset);
        }

        void refreshWaveformUi() const
        {
            gui::Waveform::updateAllSamplePoints(state);
            gui::Waveform::setAllWaveformsDirty(state);
            gui::requestMainViewRefresh(state);
        }

        [[nodiscard]] static std::vector<cupuacu::DocumentMarker>
        applyTrimMarkerTransform(
            std::vector<cupuacu::DocumentMarker> markers,
            const int64_t startFrame, const int64_t endFrame,
            const int64_t middleCount)
        {
            for (auto &marker : markers)
            {
                if (marker.frame < startFrame)
                {
                    marker.frame = 0;
                }
                else if (marker.frame >= endFrame)
                {
                    marker.frame = middleCount;
                }
                else
                {
                    marker.frame -= startFrame;
                }
            }
            return markers;
        }

        [[nodiscard]] static std::vector<cupuacu::DocumentMarker>
        applyUndoTrimMarkerTransform(
            std::vector<cupuacu::DocumentMarker> markers,
            const int64_t beforeCount, const int64_t middleCount,
            const int64_t afterCount)
        {
            const int64_t trailingInsertFrame = beforeCount + middleCount;
            for (auto &marker : markers)
            {
                marker.frame += beforeCount;
                if (marker.frame >= trailingInsertFrame)
                {
                    marker.frame += afterCount;
                }
            }
            return markers;
        }

        [[nodiscard]] cupuacu::Document buildTrimmedDocument(
            const cupuacu::Document &sourceDocument,
            const cupuacu::Document::AudioSegment &middle,
            std::vector<cupuacu::DocumentMarker> markers,
            detail::OperationProgressUi &progressUi) const
        {
            cupuacu::Document replacement;
            replacement.initialize(sourceDocument.getSampleFormat(),
                                   sourceDocument.getSampleRate(),
                                   static_cast<uint32_t>(
                                       sourceDocument.getChannelCount()),
                                   middleCount);
            detail::writeSegmentWithCancelableProgress(
                state, replacement, 0, middle, progressUi,
                "Preparing trimmed document", 0.75, 0.9);
            replacement.replaceMarkers(std::move(markers));
            replacement.adoptPreservationSourceId(
                sourceDocument.getPreservationSourceId());
            return replacement;
        }

        [[nodiscard]] cupuacu::Document buildUndoDocument(
            const cupuacu::Document &sourceDocument,
            const cupuacu::Document::AudioSegment &before,
            const cupuacu::Document::AudioSegment &middle,
            const cupuacu::Document::AudioSegment &after,
            std::vector<cupuacu::DocumentMarker> markers,
            detail::OperationProgressUi &progressUi) const
        {
            cupuacu::Document replacement;
            replacement.initialize(sourceDocument.getSampleFormat(),
                                   sourceDocument.getSampleRate(),
                                   static_cast<uint32_t>(
                                       sourceDocument.getChannelCount()),
                                   beforeCount + middleCount + afterCount);
            detail::writeSegmentWithCancelableProgress(
                state, replacement, 0, before, progressUi,
                "Preparing restored document", 0.55, 0.67);
            detail::writeSegmentWithCancelableProgress(
                state, replacement, beforeCount, middle, progressUi,
                "Preparing restored document", 0.67, 0.79);
            detail::writeSegmentWithCancelableProgress(
                state, replacement, beforeCount + middleCount, after, progressUi,
                "Preparing restored document", 0.79, 0.9);
            replacement.replaceMarkers(std::move(markers));
            replacement.adoptPreservationSourceId(
                sourceDocument.getPreservationSourceId());
            return replacement;
        }

    public:
        Trim(State *state, int64_t start, int64_t lengthToKeep)
            : DurationMutationUndoable(state), startFrame(start),
              length(lengthToKeep)
        {
            preTrimView = captureViewSnapshot(state);
            updateGui = [this]
            {
                switch (pendingViewRestore)
                {
                case PendingViewRestore::CapturePostRedo:
                    cupuacu::actions::applyDurationChangeViewPolicy(this->state);
                    postTrimView = captureViewSnapshot(this->state);
                    hasPostTrimView = true;
                    break;
                case PendingViewRestore::RestorePreUndo:
                    restoreViewSnapshot(preTrimView);
                    refreshWaveformUi();
                    break;
                case PendingViewRestore::RestorePostRedo:
                    restoreViewSnapshot(postTrimView);
                    refreshWaveformUi();
                    break;
                case PendingViewRestore::None:
                    break;
                }

                pendingViewRestore = PendingViewRestore::None;
            };
        }

        Trim(State *state, const int64_t start, const int64_t lengthToKeep,
             const int64_t beforeCountToUse, const int64_t middleCountToUse,
             const int64_t afterCountToUse,
             undo::UndoStore::SegmentHandle beforeHandleToUse,
             undo::UndoStore::SegmentHandle afterHandleToUse,
             const ViewSnapshot &preTrimViewToUse,
             const ViewSnapshot &postTrimViewToUse,
             const bool hasPostTrimViewToUse)
            : DurationMutationUndoable(state), startFrame(start),
              length(lengthToKeep), beforeCount(beforeCountToUse),
              middleCount(middleCountToUse), afterCount(afterCountToUse),
              beforeHandle(std::move(beforeHandleToUse)),
              afterHandle(std::move(afterHandleToUse)),
              preTrimView(preTrimViewToUse), postTrimView(postTrimViewToUse),
              hasPostTrimView(hasPostTrimViewToUse)
        {
            updateGui = [this]
            {
                switch (pendingViewRestore)
                {
                case PendingViewRestore::CapturePostRedo:
                    cupuacu::actions::applyDurationChangeViewPolicy(this->state);
                    postTrimView = captureViewSnapshot(this->state);
                    hasPostTrimView = true;
                    break;
                case PendingViewRestore::RestorePreUndo:
                    restoreViewSnapshot(preTrimView);
                    refreshWaveformUi();
                    break;
                case PendingViewRestore::RestorePostRedo:
                    restoreViewSnapshot(postTrimView);
                    refreshWaveformUi();
                    break;
                case PendingViewRestore::None:
                    break;
                }

                pendingViewRestore = PendingViewRestore::None;
            };
        }

        void redo() override
        {
            lastCommitted = false;
            pendingViewRestore = hasPostTrimView
                                     ? PendingViewRestore::RestorePostRedo
                                     : PendingViewRestore::CapturePostRedo;

            auto &session = state->getActiveDocumentSession();
            auto &doc = session.document;
            const int64_t ch = doc.getChannelCount();
            sampleRate = doc.getSampleRate();
            format = doc.getSampleFormat();
            oldTotal = doc.getFrameCount();

            if (startFrame < 0 || length <= 0 || startFrame >= oldTotal)
            {
                pendingViewRestore = PendingViewRestore::None;
                return;
            }

            const int64_t endFrame = std::min(startFrame + length, oldTotal);

            beforeCount = startFrame;
            middleCount = endFrame - startFrame;
            afterCount = oldTotal - endFrame;

            if (beforeCount == 0 && afterCount == 0)
            {
                lastCommitted = true;
                updateCursorPos(state, 0);
                session.selection.setValue1(0);
                session.selection.setValue2(middleCount);
                return;
            }

            cupuacu::LongTaskScope longTask(
                state, "Trimming audio", "Capturing kept selection", 0.0, false,
                true);
            cupuacu::renderLongTaskOverlayNow(state);
            detail::OperationProgressUi progressUi(state,
                                                   "Capturing kept selection");

            try
            {
                const auto endFrame = startFrame + middleCount;
                auto markers = [&]()
                {
                    auto lease = doc.acquireReadLease();
                    return applyTrimMarkerTransform(
                        std::vector<cupuacu::DocumentMarker>(
                            lease.getMarkers().begin(), lease.getMarkers().end()),
                        startFrame, endFrame, middleCount);
                }();

                const auto middle = doc.captureSegment(
                    startFrame, middleCount,
                    [&](const int64_t completed, const int64_t totalToCopy)
                    {
                        detail::publishCancelablePhaseProgress(
                            state, progressUi, "Capturing kept selection", 0.0,
                            0.35, completed, totalToCopy);
                    });
                if (beforeHandle.empty())
                {
                    beforeHandle = detail::storeSegmentIfNeeded(
                        session, beforeHandle, doc.captureSegment(0, beforeCount),
                        "trim-before");
                }
                progressUi.publishProgress("Capturing removed audio", 0.35, true);
                if (afterHandle.empty())
                {
                    afterHandle = detail::storeSegmentIfNeeded(
                        session, afterHandle,
                        doc.captureSegment(
                            endFrame, afterCount,
                            [&](const int64_t completed,
                                const int64_t totalToCopy)
                            {
                                detail::publishCancelablePhaseProgress(
                                    state, progressUi, "Capturing removed audio",
                                    0.35, 0.75, completed, totalToCopy);
                            }),
                        "trim-after");
                }
                else
                {
                    progressUi.publishProgress("Capturing removed audio", 0.75,
                                               true);
                }

                auto replacement = buildTrimmedDocument(doc, middle,
                                                        std::move(markers),
                                                        progressUi);
                cupuacu::throwIfLongTaskCanceled(state);

                cupuacu::setLongTask(state, "Trimming audio", "Committing trim",
                                     0.9, false, false);
                progressUi.publishProgress("Committing trim", 0.9, true);
                session.stopWaveformCacheBuild();
                session.document = std::move(replacement);
                session.syncSelectionAndCursorToDocumentLength();
                updateCursorPos(state, 0);
                session.selection.setValue1(0);
                session.selection.setValue2(middleCount);
                detail::rebuildWaveformCacheAfterTransactionalCommit(
                    state, session, progressUi, "Trim complete");
                session.syncSelectionAndCursorToDocumentLength();
                lastCommitted = true;
            }
            catch (const cupuacu::LongTaskCanceledError &)
            {
                pendingViewRestore = PendingViewRestore::None;
                progressUi.publishProgress("Trim canceled", 0.0, true);
                return;
            }
        }

        void undo() override
        {
            lastCommitted = false;
            pendingViewRestore = PendingViewRestore::RestorePreUndo;

            auto &session = state->getActiveDocumentSession();
            auto &doc = session.document;
            if (beforeCount == 0 && afterCount == 0)
            {
                lastCommitted = true;
                updateCursorPos(state, beforeCount);
                session.selection.setValue1(beforeCount);
                session.selection.setValue2(beforeCount + middleCount);
                return;
            }

            cupuacu::LongTaskScope longTask(
                state, "Undoing trim", "Capturing kept selection", 0.0, false,
                true);
            cupuacu::renderLongTaskOverlayNow(state);
            detail::OperationProgressUi progressUi(state,
                                                   "Capturing kept selection");

            try
            {
                const auto markers = [&]()
                {
                    auto lease = doc.acquireReadLease();
                    return applyUndoTrimMarkerTransform(
                        std::vector<cupuacu::DocumentMarker>(
                            lease.getMarkers().begin(), lease.getMarkers().end()),
                        beforeCount, middleCount, afterCount);
                }();
                const auto before = session.undoStore.readSegment(beforeHandle);
                const auto after = session.undoStore.readSegment(afterHandle);
                cupuacu::throwIfLongTaskCanceled(state);

                const auto middle = doc.captureSegment(
                    0, middleCount,
                    [&](const int64_t completed, const int64_t totalToCopy)
                    {
                        detail::publishCancelablePhaseProgress(
                            state, progressUi, "Capturing kept selection", 0.0,
                            0.55, completed, totalToCopy);
                    });

                auto replacement = buildUndoDocument(doc, before, middle, after,
                                                     std::move(markers),
                                                     progressUi);
                cupuacu::throwIfLongTaskCanceled(state);

                cupuacu::setLongTask(state, "Undoing trim", "Committing undo",
                                     0.9, false, false);
                progressUi.publishProgress("Committing undo", 0.9, true);
                session.stopWaveformCacheBuild();
                session.document = std::move(replacement);
                session.syncSelectionAndCursorToDocumentLength();
                updateCursorPos(state, beforeCount);
                session.selection.setValue1(beforeCount);
                session.selection.setValue2(beforeCount + middleCount);
                detail::rebuildWaveformCacheAfterTransactionalCommit(
                    state, session, progressUi, "Undo complete");
                session.syncSelectionAndCursorToDocumentLength();
                lastCommitted = true;
            }
            catch (const cupuacu::LongTaskCanceledError &)
            {
                pendingViewRestore = PendingViewRestore::None;
                progressUi.publishProgress("Undo canceled", 0.0, true);
                return;
            }
        }

        std::string getUndoDescription() override
        {
            return "Trim";
        }
        std::string getRedoDescription() override
        {
            return "Trim";
        }

        [[nodiscard]] bool canPersistForRestart() const override
        {
            if (beforeCount == 0 && afterCount == 0)
            {
                return true;
            }
            return !beforeHandle.empty() && !afterHandle.empty();
        }

        [[nodiscard]] std::optional<nlohmann::json>
        serializeForRestart() const override
        {
            if (!canPersistForRestart())
            {
                return std::nullopt;
            }
            return nlohmann::json{
                {"kind", "trim"},
                {"startFrame", startFrame},
                {"length", length},
                {"beforeCount", beforeCount},
                {"middleCount", middleCount},
                {"afterCount", afterCount},
                {"beforeHandle", beforeHandle.path.string()},
                {"afterHandle", afterHandle.path.string()},
                {"preTrimView",
                 {{"samplesPerPixel", preTrimView.samplesPerPixel},
                  {"verticalZoom", preTrimView.verticalZoom},
                  {"sampleOffset", preTrimView.sampleOffset}}},
                {"postTrimView",
                 {{"samplesPerPixel", postTrimView.samplesPerPixel},
                  {"verticalZoom", postTrimView.verticalZoom},
                  {"sampleOffset", postTrimView.sampleOffset}}},
                {"hasPostTrimView", hasPostTrimView},
            };
        }

        [[nodiscard]] int64_t getStartFrame() const
        {
            return startFrame;
        }

        [[nodiscard]] int64_t getLength() const
        {
            return length;
        }

        [[nodiscard]] int64_t getBeforeCount() const
        {
            return beforeCount;
        }

        [[nodiscard]] int64_t getMiddleCount() const
        {
            return middleCount;
        }

        [[nodiscard]] int64_t getAfterCount() const
        {
            return afterCount;
        }

        [[nodiscard]] const undo::UndoStore::SegmentHandle &getBeforeHandle() const
        {
            return beforeHandle;
        }

        [[nodiscard]] const undo::UndoStore::SegmentHandle &getAfterHandle() const
        {
            return afterHandle;
        }

        [[nodiscard]] const ViewSnapshot &getPreTrimView() const
        {
            return preTrimView;
        }

        [[nodiscard]] const ViewSnapshot &getPostTrimView() const
        {
            return postTrimView;
        }

        [[nodiscard]] bool getHasPostTrimView() const
        {
            return hasPostTrimView;
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
