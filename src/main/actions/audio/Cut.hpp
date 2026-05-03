#pragma once
#include "DurationMutationUndoable.hpp"
#include "../../Document.hpp"
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
            auto &session = state->getActiveDocumentSession();
            auto &doc = session.document;
            const int64_t ch = doc.getChannelCount();
            const int sr = doc.getSampleRate();
            const int64_t total = doc.getFrameCount();

            if (numFrames <= 0 || startFrame < 0 || startFrame >= total)
            {
                return;
            }

            const int64_t actualCount =
                std::min<int64_t>(numFrames, total - startFrame);
            numFrames = actualCount;

            const auto clipboardInitStartedAt =
                std::chrono::steady_clock::now();
            state->clipboard.initialize(doc.getSampleFormat(), sr, ch,
                                        numFrames);
            const auto clipboardInitializedAt =
                std::chrono::steady_clock::now();

            const auto captureStartedAt =
                std::chrono::steady_clock::now();
            removed = doc.captureSegment(startFrame, numFrames);
            const auto captureCompletedAt =
                std::chrono::steady_clock::now();

            const auto clipboardAssignStartedAt =
                std::chrono::steady_clock::now();
            state->clipboard.assignSegment(removed);
            const auto clipboardAssignedAt =
                std::chrono::steady_clock::now();

            const auto removeStartedAt =
                std::chrono::steady_clock::now();
            doc.removeFrames(startFrame, numFrames);
            const auto removeCompletedAt =
                std::chrono::steady_clock::now();

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
