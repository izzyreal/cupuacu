#include <SDL3/SDL.h>
#include "../State.h"
#include "Waveform.h"
#include "../actions/ShowOpenFileDialog.h"
#include "../actions/Play.h"
#include "../actions/Zoom.h"
#include "../actions/Save.h"

namespace cupuacu::gui {

static void updateWaveforms(cupuacu::State *state)
{
    for (auto waveform : state->waveforms)
    {
        waveform->setDirty();
        waveform->updateSamplePoints();
    }
}

static void handleKeyDown(
        SDL_Event *event,
        cupuacu::State *state)
{
    uint8_t multiplier = 1;
    uint8_t multiplierFactor = 12 / state->pixelScale;

    if (event->key.scancode == SDL_SCANCODE_ESCAPE)
    {
        actions::resetZoom(state);
        updateWaveforms(state);
        return;
    }
    
    if (event->key.mod & SDL_KMOD_SHIFT) multiplier *= multiplierFactor;
    if (event->key.mod & SDL_KMOD_ALT) multiplier *= multiplierFactor;
    if (event->key.mod & SDL_KMOD_CTRL) multiplier *= multiplierFactor;

    const auto waveformWidth = Waveform::getWaveformWidth(state);

    if (event->key.scancode == SDL_SCANCODE_Q)
    {
        if (actions::tryZoomOutHorizontally(state))
        {
            updateWaveforms(state);
        }
    }
    else if (event->key.scancode == SDL_SCANCODE_W)
    {
        if (actions::tryZoomInHorizontally(state))
        {
            updateWaveforms(state);
        }
    }
    else if (event->key.scancode == SDL_SCANCODE_E)
    {
        if (actions::tryZoomOutVertically(state, multiplier))
        {
            updateWaveforms(state);
        }
    }
    else if (event->key.scancode == SDL_SCANCODE_R)
    {
        actions::zoomInVertically(state, multiplier);
        updateWaveforms(state);
    }
    else if (event->key.scancode == SDL_SCANCODE_Z)
    {
#if __APPLE__
        if (event->key.mod & SDL_KMOD_GUI)
#else
        if (event->key.mod & SDL_KMOD_CTRL)
#endif
        {
            if (event->key.mod & SDL_KMOD_SHIFT)
            {
                state->redo();
            }
            else
            {
                state->undo();
            }

            return;
        }
        
        if (actions::tryZoomSelection(state))
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
            actions::showOpenFileDialog(state);
        }
    }
    else if (event->key.scancode == SDL_SCANCODE_S)
    {
#if __APPLE__
        if (event->key.mod & SDL_KMOD_GUI)
#else
        if (event->key.mod & SDL_KMOD_CTRL)
#endif
        {
            actions::overwrite(state);
        }
    }
    else if (event->key.scancode == SDL_SCANCODE_SPACE)
    {
        if (state->isPlaying.load())
        {
            actions::requestStop(state);
            return;
        }

        actions::play(state);
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
}
