#pragma once

#include "../Constants.h"
#include "../CupuacuState.h"

#include "../gui/Waveform.h"

static inline void snapSampleOffset(CupuacuState *state)
{
    if (state->sampleOffset < 0)
    {
        state->sampleOffset = 0;
    }
    else
    {
        // snap to nearest sample center
        state->sampleOffset = std::floor(state->sampleOffset + 0.5f);
    }
}

static void resetZoom(CupuacuState *state)
{
    const auto waveformWidth = Waveform::getWaveformWidth(state);
    state->samplesPerPixel = state->document.getFrameCount() / (float) waveformWidth;

    state->verticalZoom = INITIAL_VERTICAL_ZOOM;

    state->sampleOffset = 0;
}

static bool tryZoomInHorizontally(CupuacuState *state)
{
    if (state->samplesPerPixel <= 0.02)
    {
        return false;
    }

    state->samplesPerPixel /= 2.0;

    if (state->samplesPerPixel <= 0.02)
    {
        state->samplesPerPixel = 0.02;
    }

    snapSampleOffset(state);
    return true;
}

static bool tryZoomOutHorizontally(CupuacuState *state)
{
    if (state->samplesPerPixel >= static_cast<float>(state->document.getFrameCount()) / 20.f)
    {
        return false;
    }

    const auto waveformWidth = Waveform::getWaveformWidth(state);
    const auto centerSampleIndex =
        ((waveformWidth / 2.0 + 0.5) * state->samplesPerPixel) + state->sampleOffset;

    state->samplesPerPixel *= 2.0;

    const auto newSampleOffset =
        centerSampleIndex - ((waveformWidth / 2.0 + 0.5) * state->samplesPerPixel);

    state->sampleOffset = newSampleOffset;
    snapSampleOffset(state);
    return true;
}

static void zoomInVertically(CupuacuState *state, const uint8_t multiplier)
{
    state->verticalZoom += 0.3 * multiplier;
}

static bool tryZoomOutVertically(CupuacuState *state, const uint8_t multiplier)
{
    if (state->verticalZoom <= 1)
    {
        return false;
    }

    state->verticalZoom -= 0.3 * multiplier;

    if (state->verticalZoom < 1)
    {
        state->verticalZoom = 1;
    }

    return true;
}

