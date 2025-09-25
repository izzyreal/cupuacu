#include "CupuacuState.h"

#include "gui/Component.h"

size_t getMaxSampleOffset(CupuacuState *state)
{
        const size_t visibleSampleCount = static_cast<uint64_t>(state->waveformsOverlayHandle->getWidth() * state->samplesPerPixel);
        const size_t maxOffset = state->document.getFrameCount() - visibleSampleCount;
        printf("max offset calculated as: %zu\n", maxOffset);
        return maxOffset;
}
