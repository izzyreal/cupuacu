#include <SDL3/SDL.h>

#include "CupuacuState.h"

static void handleKeyDown(
        SDL_Event *event,
        uint32_t waveformWidth,
        CupuacuState *state,
        const double INITIAL_VERTICAL_ZOOM,
        const uint64_t INITIAL_SAMPLE_OFFSET
        )
{
    uint8_t multiplier = 1;

    if (event->key.scancode == SDL_SCANCODE_ESCAPE)
    {
        state->samplesPerPixel = state->sampleDataL.size() / (float) waveformWidth;
        state->verticalZoom = INITIAL_VERTICAL_ZOOM;
        state->sampleOffset = INITIAL_SAMPLE_OFFSET;
        return;
    }
    
    if (event->key.mod & SDL_KMOD_SHIFT) multiplier *= 2;
    if (event->key.mod & SDL_KMOD_ALT) multiplier *= 2;
    if (event->key.mod & SDL_KMOD_CTRL) multiplier *= 2;

    if (event->key.scancode == SDL_SCANCODE_Q)
    {
        if (state->samplesPerPixel < static_cast<float>(state->sampleDataL.size()) / 2.f)
        {
            state->samplesPerPixel *= 2.f;
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
        }
    }
    else if (event->key.scancode == SDL_SCANCODE_E)
    {
        state->verticalZoom -= 0.3 * multiplier;

        if (state->verticalZoom < 1)
        {
            state->verticalZoom = 1;
        }
    }
    else if (event->key.scancode == SDL_SCANCODE_R)
    {
            state->verticalZoom += 0.3 * multiplier;
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
    }
    else if (event->key.scancode == SDL_SCANCODE_RIGHT)
    {
        if (state->sampleOffset >= state->sampleDataL.size())
        {
            return;
        }

        state->sampleOffset += std::max(state->samplesPerPixel, 1.0) * multiplier;
    }
}

