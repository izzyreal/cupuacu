#include "State.h"

#include "gui/Waveform.h"
#include "actions/Undoable.h"

int64_t getMaxSampleOffset(const cupuacu::State *state)
{
    if (state->waveforms.empty() ||
        state->document.getFrameCount() == 0)
    {
        return 0;
    }

    const double waveformWidth = static_cast<double>(state->waveforms.front()->getWidth());
    const int64_t visibleSampleCount = static_cast<int64_t>(std::ceil(waveformWidth * state->samplesPerPixel));
    const int64_t frameCount = state->document.getFrameCount();
    const int64_t maxOffset = frameCount - visibleSampleCount;
    //printf("frame count: %lli, visibleSampleCount: %lli\n", frameCount, visibleSampleCount);
    return maxOffset;
}

void cupuacu::State::addUndoable(std::shared_ptr<cupuacu::actions::Undoable> undoable)
{
    undoables.push_back(undoable);
}

void cupuacu::State::undo()
{
    if (undoables.empty())
    {
        return;
    }

    auto undoable = undoables.back();
    undoables.pop_back();
    undoable->undo();
    undoable->updateGui();
    redoables.push_back(undoable);
}

void cupuacu::State::redo()
{
    if (redoables.empty())
    {
        return;
    }

    auto redoable = redoables.back();
    redoables.pop_back();
    redoable->perform();
    redoable->updateGui();
    undoables.push_back(redoable);
}

