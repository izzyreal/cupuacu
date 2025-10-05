#pragma once
#include "../Undoable.h"
#include "../../Document.h"
#include "../../gui/MainView.h"
#include "../Zoom.h"

namespace cupuacu::actions::audio {

class Cut : public Undoable {
    int64_t startFrame;
    int64_t numFrames;

    // minimal snapshot for undo
    cupuacu::Document removedRegion;

    // selection state before the cut
    double oldSel1 = 0.0;
    double oldSel2 = 0.0;
    int64_t oldCursorPos = 0;

public:
    Cut(State* state, int64_t start, int64_t count)
        : Undoable(state), startFrame(start), numFrames(count)
    {
        // Store previous selection if there was one
        if (state->selection.isActive()) {
            oldSel1 = state->selection.getStart();
            oldSel2 = state->selection.getEnd();
        }

        oldCursorPos = state->cursor;

        updateGui = [state = state] {
            resetZoom(state);
            state->mainView->setDirty();
        };
    }

    void redo() override
    {
        auto& doc = state->document;
        const int64_t ch = doc.getChannelCount();
        const int sr = doc.getSampleRate();

        // Copy selection to clipboard
        state->clipboard.initialize(state->document.getSampleFormat(), sr, ch, numFrames);
        for (int64_t c = 0; c < ch; ++c)
            for (int64_t i = 0; i < numFrames; ++i)
                state->clipboard.setSample(c, i, doc.getSample(c, startFrame + i), false);

        // Also keep for undo
        removedRegion = state->clipboard;

        // Remove from document
        doc.removeFrames(startFrame, numFrames);

        // After cutting, move cursor and clear selection
        updateCursorPos(state, startFrame);
        state->selection.reset();
    }

    void undo() override
    {
        auto& doc = state->document;
        const int64_t ch = doc.getChannelCount();

        // Reinsert the cut region
        doc.insertFrames(startFrame, numFrames);
        for (int64_t c = 0; c < ch; ++c)
            for (int64_t i = 0; i < numFrames; ++i)
                doc.setSample(c, startFrame + i, removedRegion.getSample(c, i), false);

        // Restore selection if valid
        if (oldSel1 != 0.0 || oldSel2 != 0.0) {
            state->selection.setValue1(oldSel1);
            state->selection.setValue2(oldSel2);
        }
        else
        {
            state->selection.reset();
        }

        updateCursorPos(state, oldCursorPos);
    }

    std::string getUndoDescription() override { return "Cut"; }
    std::string getRedoDescription() override { return "Cut"; }
};

} // namespace cupuacu::actions::audio

