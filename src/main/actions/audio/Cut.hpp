#pragma once
#include "DurationMutationUndoable.hpp"
#include "../../Document.hpp"
#include "../../LongTask.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

namespace cupuacu::actions::audio
{
    namespace detail
    {
        class OperationProgressUi
        {
        public:
            OperationProgressUi(State *stateToUse, std::string initialDetail)
                : state(stateToUse),
                  lastProgressDetail(std::move(initialDetail)),
                  lastProgressRenderedAt(std::chrono::steady_clock::now())
            {
            }

            void publishProgress(const std::string &detail, const double progress,
                                 const bool forceRender)
            {
                const auto now = std::chrono::steady_clock::now();
                const bool shouldRender =
                    forceRender ||
                    detail != lastProgressDetail ||
                    progress >= 1.0 ||
                    now - lastProgressRenderedAt >= kProgressUiThrottle;

                lastProgressDetail = detail;
                cupuacu::updateLongTaskOverlayOnly(state, detail, progress, false);
                if (shouldRender)
                {
                    cupuacu::renderLongTaskOverlayNow(state);
                    lastProgressRenderedAt = now;
                }
            }

            void publishPhaseProgress(const std::string &detail,
                                      const double startProgress,
                                      const double endProgress,
                                      const int64_t completed,
                                      const int64_t total)
            {
                const auto safeTotal = std::max<int64_t>(1, total);
                const double normalized =
                    std::clamp(static_cast<double>(completed) /
                                   static_cast<double>(safeTotal),
                               0.0, 1.0);
                publishProgress(
                    detail,
                    startProgress + (endProgress - startProgress) * normalized,
                    normalized >= 1.0);
            }

        private:
            static constexpr auto kProgressUiThrottle =
                std::chrono::milliseconds(50);

            State *state = nullptr;
            std::string lastProgressDetail;
            std::chrono::steady_clock::time_point lastProgressRenderedAt;
        };
    } // namespace detail
    class Cut : public DurationMutationUndoable
    {
        int64_t startFrame;
        int64_t numFrames;

        Document::AudioSegment removed;

        double oldSel1 = 0.0;
        double oldSel2 = 0.0;
        int64_t oldCursorPos = 0;

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

        void redo() override
        {
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
                state, "Cutting audio", "Capturing selection", 0.0, false);
            cupuacu::renderLongTaskOverlayNow(state);
            detail::OperationProgressUi progressUi(state, "Capturing selection");

            removed = doc.captureSegment(
                startFrame, numFrames,
                [&](const int64_t completed, const int64_t total)
                {
                    progressUi.publishPhaseProgress("Capturing selection", 0.0,
                                                    0.35, completed, total);
                });
            progressUi.publishProgress("Writing clipboard", 0.35, true);

            state->clipboard.assignSegment(
                removed,
                [&](const int64_t completed, const int64_t total)
                {
                    progressUi.publishPhaseProgress("Writing clipboard", 0.35, 0.7,
                                                    completed, total);
                });
            progressUi.publishProgress("Removing audio", 0.7, true);

            doc.removeFrames(
                startFrame, numFrames,
                [&](const int64_t completed, const int64_t total)
                {
                    progressUi.publishPhaseProgress("Removing audio", 0.7, 0.95,
                                                    completed, total);
                });
            progressUi.publishProgress("Updating waveform", 0.95, true);

            session.updateWaveformCache();
            while (true)
            {
                const auto buildProgress = session.getWaveformCacheBuildProgress();
                if (!buildProgress.has_value())
                {
                    break;
                }
                progressUi.publishPhaseProgress(
                    "Updating waveform", 0.95, 1.0,
                    buildProgress->completedBlocks,
                    std::max<int64_t>(1, buildProgress->totalBlocks));
                if (!session.pumpWaveformCacheWork())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            session.syncSelectionAndCursorToDocumentLength();

            updateCursorPos(state, startFrame);
            session.selection.reset();
            progressUi.publishProgress("Cut complete", 1.0, true);
        }

        void undo() override
        {
            auto &session = state->getActiveDocumentSession();
            auto &doc = session.document;
            cupuacu::LongTaskScope longTask(
                state, "Undoing cut", "Reinserting audio", 0.0, false);
            cupuacu::renderLongTaskOverlayNow(state);
            detail::OperationProgressUi progressUi(state, "Reinserting audio");

            doc.insertFrames(
                startFrame, numFrames,
                [&](const int64_t completed, const int64_t total)
                {
                    progressUi.publishPhaseProgress("Reinserting audio", 0.0, 0.35,
                                                    completed, total);
                });
            progressUi.publishProgress("Restoring samples", 0.35, true);

            doc.writeSegment(
                startFrame, removed, false,
                [&](const int64_t completed, const int64_t total)
                {
                    progressUi.publishPhaseProgress("Restoring samples", 0.35,
                                                    0.95, completed, total);
                });

            if (doc.getFrameCount() > 0)
            {
                session.invalidateWaveformSamples(startFrame,
                                                 doc.getFrameCount() - 1);
            }
            progressUi.publishProgress("Updating waveform", 0.95, true);
            session.updateWaveformCache();
            while (true)
            {
                const auto buildProgress = session.getWaveformCacheBuildProgress();
                if (!buildProgress.has_value())
                {
                    break;
                }
                progressUi.publishPhaseProgress(
                    "Updating waveform", 0.95, 1.0,
                    buildProgress->completedBlocks,
                    std::max<int64_t>(1, buildProgress->totalBlocks));
                if (!session.pumpWaveformCacheWork())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
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
            progressUi.publishProgress("Undo complete", 1.0, true);
        }

        std::string getUndoDescription() override
        {
            return "Cut";
        }
        std::string getRedoDescription() override
        {
            return "Cut";
        }

        [[nodiscard]] cupuacu::file::OverwritePreservationMutation
        overwritePreservationMutation() const override
        {
            return cupuacu::file::OverwritePreservationMutationHelper::compatible();
        }
    };

} // namespace cupuacu::actions::audio
