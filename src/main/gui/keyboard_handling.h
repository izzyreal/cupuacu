#include <SDL3/SDL.h>

#include "../CupuacuState.h"

#include "Waveform.h"

#include "../actions/ShowOpenFileDialog.h"
#include "../actions/Play.h"
#include "../actions/Zoom.h"

static void updateWaveform(Waveform *waveform)
{
    waveform->setDirty();
    waveform->updateSamplePoints();
}

static void handleKeyDown(
        SDL_Event *event,
        CupuacuState *state)
{
    uint8_t multiplier = 1;
    uint8_t multiplierFactor = 12 / state->hardwarePixelsPerAppPixel;

    if (event->key.scancode == SDL_SCANCODE_ESCAPE)
    {
        resetZoom(state);
        updateWaveform(state->waveform);
        return;
    }
    
    if (event->key.mod & SDL_KMOD_SHIFT) multiplier *= multiplierFactor;
    if (event->key.mod & SDL_KMOD_ALT) multiplier *= multiplierFactor;
    if (event->key.mod & SDL_KMOD_CTRL) multiplier *= multiplierFactor;

    const auto waveformWidth = state->waveform->getWidth();

    if (event->key.scancode == SDL_SCANCODE_Q)
    {
        if (state->samplesPerPixel < static_cast<float>(state->sampleDataL.size()) / 2.f)
        {
            if (tryZoomOutHorizontally(state))
            {
                updateWaveform(state->waveform);
            }
        }
    }
    else if (event->key.scancode == SDL_SCANCODE_W)
    {
        if (tryZoomInHorizontally(state))
        {
            updateWaveform(state->waveform);
        }
    }
    else if (event->key.scancode == SDL_SCANCODE_E)
    {
        if (tryZoomOutVertically(state, multiplier))
        {
            updateWaveform(state->waveform);
        }
    }
    else if (event->key.scancode == SDL_SCANCODE_R)
    {
        zoomInVertically(state, multiplier);
        updateWaveform(state->waveform);
    }
    else if (event->key.scancode == SDL_SCANCODE_Z)
    {
        if (!state->selection.isActive())
        {
            return;
        }

        state->verticalZoom = INITIAL_VERTICAL_ZOOM;
        state->sampleOffset = state->selection.getStart();
        const auto selectionLength = state->selection.getLength();
        state->samplesPerPixel = selectionLength / waveformWidth;
        updateWaveform(state->waveform);
    }
    else if (event->key.scancode == SDL_SCANCODE_LEFT)
    {
        if (state->sampleOffset == 0)
        {
            return;
        }

        state->sampleOffset -= std::max(state->samplesPerPixel, 1.0) * multiplier;
        
        if (state->sampleOffset < 0)
        {
            state->sampleOffset = 0;
        }
        updateWaveform(state->waveform);
    }
    else if (event->key.scancode == SDL_SCANCODE_RIGHT)
    {
        if (state->sampleOffset >= state->sampleDataL.size())
        {
            return;
        }

        state->sampleOffset += std::max(state->samplesPerPixel, 1.0) * multiplier;

        updateWaveform(state->waveform);
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

