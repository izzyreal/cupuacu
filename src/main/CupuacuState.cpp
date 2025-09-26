#include "CupuacuState.h"

#include "gui/Component.h"

size_t getMaxSampleOffset(const CupuacuState *state)
{
    if (state->waveformsOverlay == nullptr ||
        state->document.getFrameCount() == 0)
    {
        return 0;
    }

    const size_t visibleSampleCount = static_cast<size_t>(state->waveformsOverlay->getWidth() * state->samplesPerPixel);
    const size_t maxOffset = state->document.getFrameCount() - visibleSampleCount;
    return maxOffset;
}

