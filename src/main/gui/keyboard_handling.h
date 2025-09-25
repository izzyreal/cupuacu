#include <SDL3/SDL.h>
#include "../CupuacuState.h"
#include "Waveform.h"
#include "../actions/ShowOpenFileDialog.h"
#include "../actions/Play.h"
#include "../actions/Zoom.h"

static void updateWaveforms(CupuacuState *state)
{
    for (auto waveform : state->waveforms)
    {
        waveform->setDirty();
        waveform->updateSamplePoints();
    }
}

static void handleKeyDown(
        SDL_Event *event,
        CupuacuState *state)
{
    uint8_t multiplier = 1;
    uint8_t multiplierFactor = 12 / state->pixelScale;

    if (event->key.scancode == SDL_SCANCODE_ESCAPE)
    {
        resetZoom(state);
        updateWaveforms(state);
        return;
    }
    
    if (event->key.mod & SDL_KMOD_SHIFT) multiplier *= multiplierFactor;
    if (event->key.mod & SDL_KMOD_ALT) multiplier *= multiplierFactor;
    if (event->key.mod & SDL_KMOD_CTRL) multiplier *= multiplierFactor;

    const auto waveformWidth = Waveform::getWaveformWidth(state);

    if (event->key.scancode == SDL_SCANCODE_Q)
    {
        if (state->samplesPerPixel < static_cast<float>(state->document.getFrameCount()) / 2.f)
        {
            if (tryZoomOutHorizontally(state))
            {
                snapSampleOffset(state);
                updateWaveforms(state);
            }
        }
    }
    else if (event->key.scancode == SDL_SCANCODE_W)
    {
        if (tryZoomInHorizontally(state))
        {
            snapSampleOffset(state);
            updateWaveforms(state);
        }
    }
    else if (event->key.scancode == SDL_SCANCODE_E)
    {
        if (tryZoomOutVertically(state, multiplier))
        {
            updateWaveforms(state);
        }
    }
    else if (event->key.scancode == SDL_SCANCODE_R)
    {
        zoomInVertically(state, multiplier);
        updateWaveforms(state);
    }
    else if (event->key.scancode == SDL_SCANCODE_Z)
    {
        if (!state->selection.isActive())
        {
            return;
        }

        state->verticalZoom = INITIAL_VERTICAL_ZOOM;

        const auto waveformWidth = Waveform::getWaveformWidth(state);
        const auto selectionStart = state->selection.getStart();
        const auto selectionLength = state->selection.getLength();

        state->samplesPerPixel = selectionLength / waveformWidth;
        state->sampleOffset = selectionStart - (0.5f * state->samplesPerPixel);

        snapSampleOffset(state);
        updateWaveforms(state);
    }
    else if (event->key.scancode == SDL_SCANCODE_LEFT)
    {
        if (state->sampleOffset == 0)
        {
            return;
        }

        state->sampleOffset -= std::max(state->samplesPerPixel, 1.0) * multiplier;
        snapSampleOffset(state);
        updateWaveforms(state);
    }
    else if (event->key.scancode == SDL_SCANCODE_RIGHT)
    {
        const double maxOffset = std::max(0.0, state->document.getFrameCount() - waveformWidth * state->samplesPerPixel);
        if (state->sampleOffset >= maxOffset)
        {
            return;
        }

        state->sampleOffset += std::max(state->samplesPerPixel, 1.0) * multiplier;
        snapSampleOffset(state);
        updateWaveforms(state);
    }
    else if (event->key.scancode == SDL_SCANCODE_O)
    {
#if __APPLE__
        if (event->key.mod & SDL_KMOD_GUI)
#else
        if (event->key.mod & SDL_KMOD_CTRL)
#endif
        {
            showOpenFileDialog(state);
        }
    }
    else if (event->key.scancode == SDL_SCANCODE_SPACE)
    {
        if (state->isPlaying.load())
        {
            stop(state);
            return;
        }

        play(state);
    }
}
