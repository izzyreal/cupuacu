#pragma once
#include "DurationMutationUndoable.hpp"
#include "../../Document.hpp"
#include "../../LongTask.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace cupuacu::actions::audio
{

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
            const auto redoStartedAt =
                std::chrono::steady_clock::now();
            constexpr auto kProgressUiThrottle =
                std::chrono::milliseconds(50);
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
            std::string lastProgressDetail = "Capturing selection";
            auto lastProgressRenderedAt = std::chrono::steady_clock::now();
            const auto publishProgress =
                [&](const std::string &detail, const double progress,
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
            };
            const auto publishPhaseProgress =
                [&](const std::string &detail, const double startProgress,
                    const double endProgress, const int64_t completed,
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
            };

            const auto clipboardInitStartedAt =
                std::chrono::steady_clock::now();
            const auto clipboardInitializedAt =
                std::chrono::steady_clock::now();

            const auto captureStartedAt =
                std::chrono::steady_clock::now();
            removed = doc.captureSegment(
                startFrame, numFrames,
                [&](const int64_t completed, const int64_t total)
                {
                    publishPhaseProgress("Capturing selection", 0.0, 0.35,
                                         completed, total);
                });
            const auto captureCompletedAt =
                std::chrono::steady_clock::now();
            publishProgress("Writing clipboard", 0.35, true);

            const auto clipboardAssignStartedAt =
                std::chrono::steady_clock::now();
            state->clipboard.assignSegment(
                removed,
                [&](const int64_t completed, const int64_t total)
                {
                    publishPhaseProgress("Writing clipboard", 0.35, 0.7,
                                         completed, total);
                });
            const auto clipboardAssignedAt =
                std::chrono::steady_clock::now();
            publishProgress("Removing audio", 0.7, true);

            const auto removeStartedAt =
                std::chrono::steady_clock::now();
            doc.removeFrames(
                startFrame, numFrames,
                [&](const int64_t completed, const int64_t total)
                {
                    publishPhaseProgress("Removing audio", 0.7, 0.95,
                                         completed, total);
                });
            const auto removeCompletedAt =
                std::chrono::steady_clock::now();
            publishProgress("Updating waveform", 0.95, true);

            const auto waveformStartedAt =
                std::chrono::steady_clock::now();
            doc.updateWaveformCache();
            const auto waveformCompletedAt =
                std::chrono::steady_clock::now();

            const auto uiSyncStartedAt =
                std::chrono::steady_clock::now();
            session.syncSelectionAndCursorToDocumentLength();

            updateCursorPos(state, startFrame);
            session.selection.reset();
            const auto redoCompletedAt =
                std::chrono::steady_clock::now();
            publishProgress("Cut complete", 1.0, true);

            const auto toMilliseconds = [](const auto start, const auto end)
            {
                return std::chrono::duration_cast<std::chrono::milliseconds>(
                           end - start)
                    .count();
            };

            std::printf(
                "[cut] start=%lld frames=%lld total_frames_before=%lld "
                "clipboard_init_ms=%lld capture_ms=%lld clipboard_assign_ms=%lld "
                "remove_ms=%lld waveform_ms=%lld ui_sync_ms=%lld total_ms=%lld\n",
                static_cast<long long>(startFrame),
                static_cast<long long>(numFrames),
                static_cast<long long>(total),
                static_cast<long long>(
                    toMilliseconds(clipboardInitStartedAt,
                                   clipboardInitializedAt)),
                static_cast<long long>(
                    toMilliseconds(captureStartedAt, captureCompletedAt)),
                static_cast<long long>(
                    toMilliseconds(clipboardAssignStartedAt,
                                   clipboardAssignedAt)),
                static_cast<long long>(
                    toMilliseconds(removeStartedAt, removeCompletedAt)),
                static_cast<long long>(
                    toMilliseconds(waveformStartedAt, waveformCompletedAt)),
                static_cast<long long>(
                    toMilliseconds(uiSyncStartedAt, redoCompletedAt)),
                static_cast<long long>(
                    toMilliseconds(redoStartedAt, redoCompletedAt)));
            std::fflush(stdout);
        }

        void undo() override
        {
            auto &session = state->getActiveDocumentSession();
            auto &doc = session.document;
            doc.insertFrames(startFrame, numFrames);
            doc.writeSegment(startFrame, removed, false);

            if (doc.getFrameCount() > 0)
            {
                doc.invalidateWaveformSamples(startFrame,
                                              doc.getFrameCount() - 1);
            }
            doc.updateWaveformCache();
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
