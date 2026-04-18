#pragma once
#include "DurationMutationUndoable.hpp"
#include "../ViewPolicy.hpp"
#include "../../Document.hpp"
#include "../../gui/MainViewAccess.hpp"
#include "../../gui/Waveform.hpp"
#include <vector>
#include <cstdint>
#include <algorithm>

namespace cupuacu::actions::audio
{

    class Trim : public DurationMutationUndoable
    {
        struct ViewSnapshot
        {
            double samplesPerPixel = 1.0;
            double verticalZoom = 1.0;
            int64_t sampleOffset = 0;
        };

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

        Document::AudioSegment before;
        Document::AudioSegment after;

        ViewSnapshot preTrimView{};
        ViewSnapshot postTrimView{};
        bool hasPostTrimView = false;
        PendingViewRestore pendingViewRestore = PendingViewRestore::None;

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

        void redo() override
        {
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
                updateCursorPos(state, 0);
                session.selection.setValue1(0);
                session.selection.setValue2(middleCount);
                return;
            }

            before = doc.captureSegment(0, beforeCount);
            after = doc.captureSegment(endFrame, afterCount);

            doc.removeFrames(endFrame, afterCount);
            doc.removeFrames(0, beforeCount);
            doc.updateWaveformCache();
            session.syncSelectionAndCursorToDocumentLength();

            updateCursorPos(state, 0);
            session.selection.setValue1(0);
            session.selection.setValue2(middleCount);
        }

        void undo() override
        {
            pendingViewRestore = PendingViewRestore::RestorePreUndo;

            auto &session = state->getActiveDocumentSession();
            auto &doc = session.document;
            if (beforeCount == 0 && afterCount == 0)
            {
                updateCursorPos(state, beforeCount);
                session.selection.setValue1(beforeCount);
                session.selection.setValue2(beforeCount + middleCount);
                return;
            }

            doc.insertFrames(0, beforeCount);
            doc.writeSegment(0, before, false);

            doc.insertFrames(beforeCount + middleCount, afterCount);
            doc.writeSegment(beforeCount + middleCount, after, false);

            if (doc.getFrameCount() > 0)
            {
                doc.invalidateWaveformSamples(0, doc.getFrameCount() - 1);
            }
            doc.updateWaveformCache();
            session.syncSelectionAndCursorToDocumentLength();

            updateCursorPos(state, beforeCount);
            session.selection.setValue1(beforeCount);
            session.selection.setValue2(beforeCount + middleCount);
        }

        std::string getUndoDescription() override
        {
            return "Trim";
        }
        std::string getRedoDescription() override
        {
            return "Trim";
        }

        [[nodiscard]] cupuacu::file::OverwritePreservationMutation
        overwritePreservationMutation() const override
        {
            return cupuacu::file::OverwritePreservationMutationHelper::compatible();
        }
    };

} // namespace cupuacu::actions::audio
