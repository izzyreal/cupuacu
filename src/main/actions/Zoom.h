#pragma once

#include "../Constants.h"
#include "../CupuacuState.h"

#include "../gui/Waveform.h"

static void resetZoom(CupuacuState *state)
{
    const auto waveformWidth = state->waveform->getWidth();
    state->samplesPerPixel = state->document.getFrameCount() / (float) waveformWidth;
    state->verticalZoom = INITIAL_VERTICAL_ZOOM;
    state->sampleOffset = INITIAL_SAMPLE_OFFSET;
}

static bool tryZoomInHorizontally(CupuacuState *state)
{
    if (state->samplesPerPixel <= 0.02)
    {
        return false;
    }

    state->samplesPerPixel /= 2.f;

    if (state->samplesPerPixel <= 0.02)
    {
        state->samplesPerPixel = 0.02;
    }

    return true;
}

static bool tryZoomOutHorizontally(CupuacuState *state)
{
    // We return in a pretty arbitrary condition. Maybe at some point we want to make it so that
    // the waveform can't be smaller than the window width. For now we allow the waveform to
    // become "pretty small", but not any smaller.
    if (state->samplesPerPixel >= static_cast<float>(state->document.getFrameCount()) / 20.f)
    {
        return false;
    }

    const auto waveformWidth = state->waveform->getWidth();
    const auto centerSampleIndex = ((waveformWidth / 2.f) * state->samplesPerPixel) + state->sampleOffset;
    state->samplesPerPixel *= 2.f;
    const auto newSampleOffset = centerSampleIndex - ((waveformWidth / 2.f) * state->samplesPerPixel);
    state->sampleOffset = newSampleOffset < 0 ? 0 : newSampleOffset;
    return true;
}

// Currently unlimited, so we should probably decide on some sort of limit. 
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

