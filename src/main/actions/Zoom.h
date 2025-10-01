#pragma once

#include "../Constants.h"
#include "../CupuacuState.h"
#include "../gui/Waveform.h"

static double getMinSamplesPerPixel(const CupuacuState *state)
{
    const auto waveformWidth = Waveform::getWaveformWidth(state);
    return 1.0 / waveformWidth;
}

static void resetZoom(CupuacuState *state)
{
    const auto waveformWidth = Waveform::getWaveformWidth(state);
    if (waveformWidth == 0)
    {
        state->samplesPerPixel = 0;
    }
    else
    {
        state->samplesPerPixel = state->document.getFrameCount() / (float) waveformWidth;
    }

    state->verticalZoom = INITIAL_VERTICAL_ZOOM;

    resetSampleValueUnderMouseCursor(state);

    for (auto w : state->waveforms)
    {
        w->clearHighlight();
    }

    state->sampleOffset = 0;
}

static bool tryZoomInHorizontally(CupuacuState *state)
{
    const double minSamplesPerPixel = getMinSamplesPerPixel(state);

    if (state->samplesPerPixel <= minSamplesPerPixel)
    {
        return false;
    }

    state->samplesPerPixel = std::max(state->samplesPerPixel / 2.0, minSamplesPerPixel);

    resetSampleValueUnderMouseCursor(state);

    for (auto w : state->waveforms)
    {
        w->clearHighlight();
    }

    return true;
}

static bool tryZoomOutHorizontally(CupuacuState *state)
{
    const auto waveformWidth = Waveform::getWaveformWidth(state);
    const float maxSamplesPerPixel = static_cast<float>(state->document.getFrameCount()) / waveformWidth;

    if (state->samplesPerPixel >= maxSamplesPerPixel)
    {
        return false;
    }

    const auto centerSampleIndex = ((waveformWidth / 2.0 + 0.5) * state->samplesPerPixel) + state->sampleOffset;

    state->samplesPerPixel = std::min(state->samplesPerPixel * 2.0, static_cast<double>(maxSamplesPerPixel));

    const auto newSampleOffset = centerSampleIndex - ((waveformWidth / 2.0 + 0.5) * state->samplesPerPixel);

    updateSampleOffset(state, newSampleOffset);

    resetSampleValueUnderMouseCursor(state);

    for (auto w : state->waveforms)
    {
        w->clearHighlight();
    }
    
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

static bool tryZoomSelection(CupuacuState *state)
{
    if (!state->selection.isActive() || state->selection.getLengthInt() < 1)
    {
        return false;
    }

    state->verticalZoom = INITIAL_VERTICAL_ZOOM;

    const auto waveformWidth = Waveform::getWaveformWidth(state);
    const auto selectionLength = state->selection.getLengthInt();

    state->samplesPerPixel = selectionLength / static_cast<double>(waveformWidth);
    state->sampleOffset = state->selection.getStartInt();
    return true;
}

