#include <SDL3/SDL.h>

#include "../CupuacuState.h"

#include "Waveform.h"

#include "../actions/ShowOpenFileDialog.h"
#include "../actions/Play.h"

static void updateWaveform(Waveform *waveform, CupuacuState *state)
{
    waveform->setDirty();
    waveform->updateSamplePoints();
}

static void handleKeyDown(
        SDL_Event *event,
        uint32_t waveformWidth,
        CupuacuState *state,
        const double INITIAL_VERTICAL_ZOOM,
        const uint64_t INITIAL_SAMPLE_OFFSET,
        Waveform *waveform)
{
    uint8_t multiplier = 1;
    uint8_t multiplierFactor = 12 / state->hardwarePixelsPerAppPixel;

    if (event->key.scancode == SDL_SCANCODE_ESCAPE)
    {
        state->samplesPerPixel = state->sampleDataL.size() / (float) waveformWidth;
        state->verticalZoom = INITIAL_VERTICAL_ZOOM;
        state->sampleOffset = INITIAL_SAMPLE_OFFSET;
        updateWaveform(waveform, state);
        return;
    }
    
    if (event->key.mod & SDL_KMOD_SHIFT) multiplier *= multiplierFactor;
    if (event->key.mod & SDL_KMOD_ALT) multiplier *= multiplierFactor;
    if (event->key.mod & SDL_KMOD_CTRL) multiplier *= multiplierFactor;

    if (event->key.scancode == SDL_SCANCODE_Q)
    {
        if (state->samplesPerPixel < static_cast<float>(state->sampleDataL.size()) / 2.f)
        {
            const auto centerSampleIndex = ((waveform->getWidth() / 2.f) * state->samplesPerPixel) + state->sampleOffset;
            state->samplesPerPixel *= 2.f;
            const auto newSampleOffset = centerSampleIndex - ((waveform->getWidth() / 2.f) * state->samplesPerPixel);
            state->sampleOffset = newSampleOffset < 0 ? 0 : newSampleOffset;
            updateWaveform(waveform, state);
        }
    }
    else if (event->key.scancode == SDL_SCANCODE_W)
    {
        if (state->samplesPerPixel > 0.02)
        {
            state->samplesPerPixel /= 2.f;

            if (state->samplesPerPixel <= 0.02)
            {
                state->samplesPerPixel = 0.02;
            }
            updateWaveform(waveform, state);
        }
    }
    else if (event->key.scancode == SDL_SCANCODE_E)
    {
        state->verticalZoom -= 0.3 * multiplier;

        if (state->verticalZoom < 1)
        {
            state->verticalZoom = 1;
        }
        updateWaveform(waveform, state);
    }
    else if (event->key.scancode == SDL_SCANCODE_R)
    {
        state->verticalZoom += 0.3 * multiplier;
        updateWaveform(waveform, state);
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
        state->samplesPerPixel = selectionLength / waveform->getWidth();
        updateWaveform(waveform, state);
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
        updateWaveform(waveform, state);
    }
    else if (event->key.scancode == SDL_SCANCODE_RIGHT)
    {
        if (state->sampleOffset >= state->sampleDataL.size())
        {
            return;
        }

        state->sampleOffset += std::max(state->samplesPerPixel, 1.0) * multiplier;

        updateWaveform(waveform, state);
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

