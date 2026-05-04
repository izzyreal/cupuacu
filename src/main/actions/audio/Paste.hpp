#pragma once
#include "DurationMutationUndoable.hpp"
#include "../../Document.hpp"
#include <algorithm>
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

        Document::AudioSegment inserted;
        Document::AudioSegment overwritten;

        bool hadOldSelection = false;
        double oldSel1 = 0;
        double oldSel2 = 0;
        int64_t oldCursorPos = 0;

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

        void redo() override
        {
            auto &session = state->getActiveDocumentSession();
            const auto &clip = state->clipboard;
            if (clip.getFrameCount() == 0)
            {
                return;
            }

            auto &doc = session.document;
            const int64_t ch = doc.getChannelCount();
            const int64_t clipFrames = clip.getFrameCount();
            const int64_t docFrames = doc.getFrameCount();

            if (startFrame < 0 || startFrame > docFrames)
            {
                return;
            }

            insertedFrameCount = clipFrames;
            inserted = clip.captureSegment(0, insertedFrameCount);

            overwrittenFrameCount = 0;
            overwritten = {};

            if (endFrame >= 0 && endFrame > startFrame)
            {
                overwrittenFrameCount =
                    std::min(endFrame - startFrame, docFrames - startFrame);
                overwrittenFrameCount =
                    std::max<int64_t>(0, overwrittenFrameCount);

                overwritten = {};
                if (overwrittenFrameCount > 0)
                {
                    overwritten = doc.captureSegment(startFrame, overwrittenFrameCount);

                    doc.removeFrames(startFrame, overwrittenFrameCount);
                }

                doc.insertFrames(startFrame, insertedFrameCount);
            }
            else
            {
                doc.insertFrames(startFrame, insertedFrameCount);
            }

            doc.writeSegment(startFrame, inserted, false);

            if (doc.getFrameCount() > 0)
            {
                session.invalidateWaveformSamples(startFrame,
                                                 doc.getFrameCount() - 1);
            }
            session.updateWaveformCache();
            session.syncSelectionAndCursorToDocumentLength();

            session.selection.setValue1(startFrame);
            session.selection.setValue2(startFrame + insertedFrameCount);
            updateCursorPos(state, startFrame);
        }

        void undo() override
        {
            auto &session = state->getActiveDocumentSession();
            auto &doc = session.document;
            const int64_t ch = doc.getChannelCount();
            const int64_t docFrames = doc.getFrameCount();

            if (startFrame < 0 || startFrame >= docFrames)
            {
                return;
            }

            const int64_t removeCount =
                std::min<int64_t>(insertedFrameCount, docFrames - startFrame);
            doc.removeFrames(startFrame, removeCount);

            if (endFrame >= 0 && overwrittenFrameCount > 0)
            {
                doc.insertFrames(startFrame, overwrittenFrameCount);
                doc.writeSegment(startFrame, overwritten, false);
            }

            if (doc.getFrameCount() > 0)
            {
                session.invalidateWaveformSamples(startFrame,
                                                 doc.getFrameCount() - 1);
            }
            session.updateWaveformCache();
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
        }

        std::string getUndoDescription() override
        {
            return (endFrame >= 0 ? "Paste overwrite" : "Paste insert");
        }

        std::string getRedoDescription() override
        {
            return getUndoDescription();
        }

        [[nodiscard]] cupuacu::file::OverwritePreservationMutation
        overwritePreservationMutation() const override
        {
            return cupuacu::file::OverwritePreservationMutationHelper::compatible();
        }
    };

} // namespace cupuacu::actions::audio
