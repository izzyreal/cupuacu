#include "CupuacuState.h"

#include "gui/Component.h"

size_t getMaxSampleOffset(const CupuacuState *state)
{
    if (state->waveformsOverlayHandle == nullptr ||
        state->document.getFrameCount() == 0)
    {
        return 0;
    }

    const size_t visibleSampleCount = static_cast<size_t>(state->waveformsOverlayHandle->getWidth() * state->samplesPerPixel);
    const size_t maxOffset = state->document.getFrameCount() - visibleSampleCount;
    return maxOffset;
}

