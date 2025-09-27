#include "CupuacuState.h"

#include "gui/Waveform.h"

int64_t getMaxSampleOffset(const CupuacuState *state)
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

