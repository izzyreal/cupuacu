#pragma once
#include "../Undoable.h"
#include "../../Document.h"
#include "../../gui/MainView.h"

namespace cupuacu::actions::audio {

class Copy : public Undoable {
    int64_t startFrame;
    int64_t numFrames;

    // For restoring state on undo (selection, cursor)
    double oldSel1 = 0;
    double oldSel2 = 0;
    int64_t oldCursorPos = 0;

public:
    Copy(State* state, int64_t start, int64_t count)
        : Undoable(state), startFrame(start), numFrames(count)
    {
        if (state->selection.isActive())
        {
            oldSel1 = state->selection.getStart();
            oldSel2 = state->selection.getEnd();
        }
        oldCursorPos = state->cursor;

        updateGui = [state = state] {
            state->mainView->setDirty();
        };
    }

    void redo() override
    {
        auto& doc = state->document;
        const int64_t ch = doc.getChannelCount();
        const int sr = doc.getSampleRate();
        const int64_t totalFrames = doc.getFrameCount();

        if (numFrames <= 0 || startFrame < 0 || startFrame >= totalFrames)
            return;

        const int64_t maxCopy = std::min<int64_t>(numFrames, totalFrames - startFrame);

        // Copy selection to clipboard
        state->clipboard.initialize(doc.getSampleFormat(), sr, ch, maxCopy);
        for (int64_t c = 0; c < ch; ++c)
            for (int64_t i = 0; i < maxCopy; ++i)
                state->clipboard.setSample(c, i, doc.getSample(c, startFrame + i), false);

        // Keep the same selection and cursor position
        state->selection.setValue1(startFrame);
        state->selection.setValue2(startFrame + maxCopy);
        updateCursorPos(state, startFrame);
    }

    void undo() override
    {
        // Copy doesn’t modify audio data — just restore previous UI state
        if (oldSel1 != 0 && oldSel2 != 0)
        {
            state->selection.setValue1(oldSel1);
            state->selection.setValue2(oldSel2);
        }
        else
        {
            state->selection.reset();
        }
        updateCursorPos(state, oldCursorPos);
    }

    std::string getUndoDescription() override { return "Copy"; }
    std::string getRedoDescription() override { return "Copy"; }
};

} // namespace cupuacu::actions::audio
