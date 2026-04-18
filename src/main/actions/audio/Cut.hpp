#pragma once
#include "DurationMutationUndoable.hpp"
#include "../../Document.hpp"
#include <vector>
#include <cstdint>
#include <algorithm>

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

            state->clipboard.initialize(doc.getSampleFormat(), sr, ch,
                                        numFrames);

            removed = doc.captureSegment(startFrame, numFrames);
            state->clipboard.assignSegment(removed);

            doc.removeFrames(startFrame, numFrames);
            doc.updateWaveformCache();
            session.syncSelectionAndCursorToDocumentLength();

            updateCursorPos(state, startFrame);
            session.selection.reset();
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
