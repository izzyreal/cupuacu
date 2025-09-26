#include "CupuacuState.h"

#include "gui/MainView.h"

int64_t getMaxSampleOffset(const CupuacuState *state)
{
    if (state->mainView == nullptr ||
        state->document.getFrameCount() == 0)
    {
        return 0;
    }

    const int64_t visibleSampleCount = static_cast<int64_t>(state->mainView->getWidth() * state->samplesPerPixel);
    const int64_t maxOffset = state->document.getFrameCount() - visibleSampleCount;
    return maxOffset;
}

