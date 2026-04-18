#pragma once
#include "../Undoable.hpp"
#include "../../Document.hpp"
#include "../../gui/MainViewAccess.hpp"

namespace cupuacu::actions::audio
{

    class Copy : public Undoable
    {
        int64_t startFrame;
        int64_t numFrames;

        // For restoring state on undo (selection, cursor)
        bool hadOldSelection = false;
        double oldSel1 = 0;
        double oldSel2 = 0;
        int64_t oldCursorPos = 0;

    public:
        Copy(State *state, int64_t start, int64_t count)
            : Undoable(state), startFrame(start), numFrames(count)
        {
            auto &session = state->getActiveDocumentSession();
            if (session.selection.isActive())
            {
                hadOldSelection = true;
                oldSel1 = session.selection.getStart();
                oldSel2 = session.selection.getEnd();
            }
            oldCursorPos = session.cursor;

            updateGui = [state = state]
            {
                gui::requestMainViewRefresh(state);
            };
        }

        void redo() override
        {
            auto &session = state->getActiveDocumentSession();
            auto &doc = session.document;
            const int64_t ch = doc.getChannelCount();
            const int sr = doc.getSampleRate();
            const int64_t totalFrames = doc.getFrameCount();

            if (numFrames <= 0 || startFrame < 0 || startFrame >= totalFrames)
            {
                return;
            }

            const int64_t maxCopy =
                std::min<int64_t>(numFrames, totalFrames - startFrame);

            // Copy selection to clipboard
            state->clipboard.assignSegment(doc.captureSegment(startFrame, maxCopy));

            // Keep the same selection and cursor position
            session.selection.setValue1(startFrame);
            session.selection.setValue2(startFrame + maxCopy);
            updateCursorPos(state, startFrame);
        }

        void undo() override
        {
            auto &session = state->getActiveDocumentSession();
            // Copy doesn’t modify audio data — just restore previous UI state
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
            return "Copy";
        }
        std::string getRedoDescription() override
        {
            return "Copy";
        }

        [[nodiscard]] cupuacu::file::OverwritePreservationMutation
        overwritePreservationMutation() const override
        {
            return cupuacu::file::OverwritePreservationMutationHelper::compatible();
        }
    };

} // namespace cupuacu::actions::audio
