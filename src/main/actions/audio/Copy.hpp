#pragma once
#include "../Undoable.hpp"
#include "../../Document.hpp"
#include "../../gui/MainView.hpp"

namespace cupuacu::actions::audio
{

    class Copy : public Undoable
    {
        int64_t startFrame;
        int64_t numFrames;

        // For restoring state on undo (selection, cursor)
        double oldSel1 = 0;
        double oldSel2 = 0;
        int64_t oldCursorPos = 0;

    public:
        Copy(State *state, int64_t start, int64_t count)
            : Undoable(state), startFrame(start), numFrames(count)
        {
            auto &session = state->activeDocumentSession;
            if (session.selection.isActive())
            {
                oldSel1 = session.selection.getStart();
                oldSel2 = session.selection.getEnd();
            }
            oldCursorPos = session.cursor;

            updateGui = [state = state]
            {
                state->mainView->setDirty();
            };
        }

        void redo() override
        {
            auto &session = state->activeDocumentSession;
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
            state->clipboard.initialize(doc.getSampleFormat(), sr, ch, maxCopy);
            for (int64_t c = 0; c < ch; ++c)
            {
                for (int64_t i = 0; i < maxCopy; ++i)
                {
                    state->clipboard.setSample(
                        c, i, doc.getSample(c, startFrame + i), false);
                }
            }

            // Keep the same selection and cursor position
            session.selection.setValue1(startFrame);
            session.selection.setValue2(startFrame + maxCopy);
            updateCursorPos(state, startFrame);
        }

        void undo() override
        {
            auto &session = state->activeDocumentSession;
            // Copy doesn’t modify audio data — just restore previous UI state
            if (oldSel1 != 0 && oldSel2 != 0)
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
    };

} // namespace cupuacu::actions::audio
