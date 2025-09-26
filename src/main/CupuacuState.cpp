#include "CupuacuState.h"

#include "gui/MainView.h"

size_t getMaxSampleOffset(const CupuacuState *state)
{
    if (state->mainView == nullptr ||
        state->document.getFrameCount() == 0)
    {
        return 0;
    }

    const size_t visibleSampleCount = static_cast<size_t>(state->mainView->getWidth() * state->samplesPerPixel);
    const size_t maxOffset = state->document.getFrameCount() - visibleSampleCount;
    return maxOffset;
}

