#pragma once
#include "../Undoable.h"
#include "../../Document.h"
#include "../../gui/MainView.h"
#include "../Zoom.h"

namespace cupuacu::actions::audio {

class Trim : public Undoable {
    int64_t startFrame;
    int64_t length;

    cupuacu::Document beforeRegion;
    cupuacu::Document afterRegion;

public:
    Trim(State* state, int64_t start, int64_t lengthToKeep)
        : Undoable(state), startFrame(start), length(lengthToKeep)
    {
        updateGui = [state = state, length = length] {
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

        if (startFrame < 0 || length <= 0 || startFrame >= total)
            return;

        const int64_t endFrame = std::min(startFrame + length, total);

        const int64_t beforeCount = std::max<int64_t>(0, startFrame);
        const int64_t afterCount = std::max<int64_t>(0, total - endFrame);

        // backup removed parts
        if (beforeCount > 0) {
            beforeRegion.initialize(doc.getSampleFormat(), sr, ch, beforeCount);
            for (int64_t c = 0; c < ch; ++c)
                for (int64_t i = 0; i < beforeCount; ++i)
                    beforeRegion.setSample(c, i, doc.getSample(c, i), false);
        }

        if (afterCount > 0) {
            afterRegion.initialize(doc.getSampleFormat(), sr, ch, afterCount);
            for (int64_t c = 0; c < ch; ++c)
                for (int64_t i = 0; i < afterCount; ++i)
                    afterRegion.setSample(c, i, doc.getSample(c, endFrame + i), false);
        }

        // replace doc with kept region
        const int64_t newCount = endFrame - startFrame;
        cupuacu::Document newDoc;
        newDoc.initialize(doc.getSampleFormat(), sr, ch, newCount);
        for (int64_t c = 0; c < ch; ++c)
            for (int64_t i = 0; i < newCount; ++i)
                newDoc.setSample(c, i, doc.getSample(c, startFrame + i), false);

        doc = std::move(newDoc);
        updateCursorPos(state, 0);
        state->selection.setValue1(0);
        state->selection.setValue2(length);
    }

    void undo() override
    {
        auto& doc = state->document;
        const int64_t ch = doc.getChannelCount();
        const int sr = doc.getSampleRate();

        const int64_t beforeCount = beforeRegion.getFrameCount();
        const int64_t afterCount = afterRegion.getFrameCount();
        const int64_t middleCount = doc.getFrameCount();
        const int64_t total = beforeCount + middleCount + afterCount;

        cupuacu::Document restored;
        restored.initialize(doc.getSampleFormat(), sr, ch, total);

        // before
        for (int64_t c = 0; c < ch; ++c)
            for (int64_t i = 0; i < beforeCount; ++i)
                restored.setSample(c, i, beforeRegion.getSample(c, i), false);
        // middle
        for (int64_t c = 0; c < ch; ++c)
            for (int64_t i = 0; i < middleCount; ++i)
                restored.setSample(c, beforeCount + i, doc.getSample(c, i), false);
        // after
        for (int64_t c = 0; c < ch; ++c)
            for (int64_t i = 0; i < afterCount; ++i)
                restored.setSample(c, beforeCount + middleCount + i, afterRegion.getSample(c, i), false);

        doc = std::move(restored);
        updateCursorPos(state, beforeCount);
        state->selection.setValue1(beforeCount);
        state->selection.setValue2(beforeCount + middleCount);
    }

    std::string getUndoDescription() override { return "Trim"; }
    std::string getRedoDescription() override { return "Trim"; }
};

} // namespace cupuacu::actions::audio

