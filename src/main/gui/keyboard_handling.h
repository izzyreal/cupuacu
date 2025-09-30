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
        if (tryZoomOutHorizontally(state))
        {
            updateWaveforms(state);
        }
    }
    else if (event->key.scancode == SDL_SCANCODE_W)
    {
        if (tryZoomInHorizontally(state))
        {
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
        if (tryZoomSelection(state))
        {
            updateWaveforms(state);
        }
    }
    else if (event->key.scancode == SDL_SCANCODE_LEFT)
    {
        if (state->sampleOffset == 0)
        {
            return;
        }

        const int64_t oldSampleOffset = state->sampleOffset;

        updateSampleOffset(state, state->sampleOffset - std::max(state->samplesPerPixel, 1.0) * multiplier);

        if (oldSampleOffset == state->sampleOffset)
        {
            return;
        }

        resetSampleValueUnderMouseCursor(state);

        for (auto w : state->waveforms)
        {
            w->clearHighlight();
        }
        
        updateWaveforms(state);
    }
    else if (event->key.scancode == SDL_SCANCODE_RIGHT)
    {
        const int64_t oldSampleOffset = state->sampleOffset;

        updateSampleOffset(state, state->sampleOffset + std::max(state->samplesPerPixel, 1.0) * multiplier);
        
        if (oldSampleOffset == state->sampleOffset)
        {
            return;
        }

        resetSampleValueUnderMouseCursor(state);

        for (auto w : state->waveforms)
        {
            w->clearHighlight();
        }
        
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
    else if (event->key.scancode == SDL_SCANCODE_PERIOD &&
             (event->key.mod & SDL_KMOD_SHIFT))
    {
        if (state->pixelScale < 4)
        {
            state->pixelScale = std::min<uint8_t>(state->pixelScale * 2, 4);

            const double newSamplesPerPixel = state->samplesPerPixel * 2;
            
            buildComponents(state);
            
            state->samplesPerPixel = newSamplesPerPixel;
            
            for (auto &w : state->waveforms)
            {
                w->setDirty();
            }
        }
    }
    else if (event->key.scancode == SDL_SCANCODE_COMMA &&
             (event->key.mod & SDL_KMOD_SHIFT))
    {
        if (state->pixelScale > 1)
        {
            state->pixelScale = std::max<uint8_t>(state->pixelScale / 2, 1);

            const double newSamplesPerPixel = state->samplesPerPixel / 2;
            
            buildComponents(state);

            state->samplesPerPixel = newSamplesPerPixel;

            for (auto &w : state->waveforms)
            {
                w->setDirty();
            }
        }
    }
}
