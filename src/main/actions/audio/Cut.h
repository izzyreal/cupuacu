#pragma once
#include "../Undoable.h"
#include "../../Document.h"
#include "../../gui/MainView.h"
#include "../Zoom.h"
#include <vector>
#include <cstdint>
#include <algorithm>

namespace cupuacu::actions::audio {

class Cut : public Undoable {
    int64_t startFrame;
    int64_t numFrames;

    std::vector<std::vector<float>> removed;

    double oldSel1 = 0.0;
    double oldSel2 = 0.0;
    int64_t oldCursorPos = 0;

public:
    Cut(State* state, int64_t start, int64_t count)
        : Undoable(state), startFrame(start), numFrames(count)
    {
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
        const int64_t total = doc.getFrameCount();

        if (numFrames <= 0 || startFrame < 0 || startFrame >= total)
            return;

        const int64_t actualCount = std::min<int64_t>(numFrames, total - startFrame);
        numFrames = actualCount;

        state->clipboard.initialize(doc.getSampleFormat(), sr, ch, numFrames);

        removed.assign((size_t)ch, {});
        for (int64_t c = 0; c < ch; ++c)
        {
            removed[(size_t)c].resize((size_t)numFrames);
            for (int64_t i = 0; i < numFrames; ++i)
            {
                float v = doc.getSample(c, startFrame + i);
                removed[(size_t)c][(size_t)i] = v;
                state->clipboard.setSample(c, i, v, false);
            }
        }

        doc.removeFrames(startFrame, numFrames);
        doc.updateWaveformCache();

        updateCursorPos(state, startFrame);
        state->selection.reset();
    }

    void undo() override
    {
        auto& doc = state->document;
        const int64_t ch = doc.getChannelCount();

        doc.insertFrames(startFrame, numFrames);

        for (int64_t c = 0; c < ch; ++c)
            for (int64_t i = 0; i < numFrames; ++i)
                doc.setSample(c, startFrame + i, removed[(size_t)c][(size_t)i], false);

        doc.updateWaveformCache();

        if (oldSel1 != 0.0 || oldSel2 != 0.0) {
            state->selection.setValue1(oldSel1);
            state->selection.setValue2(oldSel2);
        } else {
            state->selection.reset();
        }

        updateCursorPos(state, oldCursorPos);
    }

    std::string getUndoDescription() override { return "Cut"; }
    std::string getRedoDescription() override { return "Cut"; }
};

} // namespace cupuacu::actions::audio

