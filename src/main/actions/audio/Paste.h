#pragma once
#include "../Undoable.h"
#include "../../Document.h"
#include <algorithm>
#include "../Zoom.h"
#include "../../gui/MainView.h"

namespace cupuacu::actions::audio {

class Paste : public Undoable {
    int64_t startFrame;
    int64_t endFrame; // -1 = insert mode

    int64_t overwrittenFrameCount = 0;
    cupuacu::Document insertedData;
    cupuacu::Document overwrittenData;

    // Selection & cursor before paste
    double oldSel1 = 0;
    double oldSel2 = 0;
    int64_t oldCursorPos = 0;

public:
    Paste(State* state, int64_t start, int64_t end = -1)
        : Undoable(state), startFrame(start), endFrame(end)
    {
        // store the pre-paste editing context
        if (state->selection.isActive())
        {
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
        const auto& clip = state->clipboard;
        if (clip.getFrameCount() == 0)
            return;

        auto& doc = state->document;
        const int64_t ch = doc.getChannelCount();
        const int64_t clipFrames = clip.getFrameCount();
        const int64_t docFrames = doc.getFrameCount();

        if (startFrame < 0 || startFrame > docFrames)
            return; // invalid paste location

        insertedData = clip;

        if (endFrame >= 0 && endFrame > startFrame)
        {
            // --- Overwrite mode ---
            overwrittenFrameCount = std::min(endFrame - startFrame, docFrames - startFrame);
            overwrittenFrameCount = std::max<int64_t>(0, overwrittenFrameCount);

            // Backup region being replaced
            if (overwrittenFrameCount > 0) {
                overwrittenData.initialize(doc.getSampleFormat(), doc.getSampleRate(), ch, overwrittenFrameCount);
                for (int64_t c = 0; c < ch; ++c)
                    for (int64_t i = 0; i < overwrittenFrameCount; ++i)
                        overwrittenData.setSample(c, i, doc.getSample(c, startFrame + i), false);

                // Remove the old region
                doc.removeFrames(startFrame, overwrittenFrameCount);
            }

            // Insert clipboard frames (new region)
            doc.insertFrames(startFrame, clipFrames);
        }
        else
        {
            // --- Insert mode ---
            doc.insertFrames(startFrame, clipFrames);
        }

        // Final safety check: prevent out-of-range writes
        const int64_t maxWritable = std::min<int64_t>(clipFrames, doc.getFrameCount() - startFrame);
        for (int64_t c = 0; c < ch; ++c)
            for (int64_t i = 0; i < maxWritable; ++i)
                doc.setSample(c, startFrame + i, clip.getSample(c, i), false);

        // --- Select the newly pasted region ---
        state->selection.setValue1(startFrame);
        state->selection.setValue2(startFrame + clipFrames);
        updateCursorPos(state, startFrame);
    }

    void undo() override
    {
        auto& doc = state->document;
        const int64_t ch = doc.getChannelCount();
        const int64_t docFrames = doc.getFrameCount();
        const int64_t clipFrames = insertedData.getFrameCount();

        if (startFrame < 0 || startFrame >= docFrames)
            return;

        // Remove pasted region
        const int64_t removeCount = std::min<int64_t>(clipFrames, docFrames - startFrame);
        doc.removeFrames(startFrame, removeCount);

        // If overwrite, restore original region
        if (endFrame >= 0 && overwrittenData.getFrameCount() > 0) {
            doc.insertFrames(startFrame, overwrittenData.getFrameCount());
            const int64_t maxRestore = std::min<int64_t>(overwrittenData.getFrameCount(), doc.getFrameCount() - startFrame);
            for (int64_t c = 0; c < ch; ++c)
                for (int64_t i = 0; i < maxRestore; ++i)
                    doc.setSample(c, startFrame + i, overwrittenData.getSample(c, i), false);
        }

        // --- Restore previous selection & cursor ---
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

    std::string getUndoDescription() override
    {
        return (endFrame >= 0 ? "Paste overwrite" : "Paste insert");
    }

    std::string getRedoDescription() override { return getUndoDescription(); }
};

} // namespace cupuacu::actions::audio

