#include <SDL3/SDL.h>

#include "CupuacuState.h"

#include <functional>

static void handleKeyDown(
        SDL_Event *event,
        SDL_Texture *canvas,
        CupuacuState *state,
        const double INITIAL_VERTICAL_ZOOM,
        const uint64_t INITIAL_SAMPLE_OFFSET,
        const std::function<void(CupuacuState*)> &paintAndRenderWaveform
        )
{
    uint8_t multiplier = 1;

    if (event->key.scancode == SDL_SCANCODE_ESCAPE)
    {
        SDL_FPoint canvasDimensions;
        SDL_GetTextureSize(canvas, &canvasDimensions.x, &canvasDimensions.y);
        state->samplesPerPixel = state->sampleDataL.size() / canvasDimensions.x;
        state->verticalZoom = INITIAL_VERTICAL_ZOOM;
        state->sampleOffset = INITIAL_SAMPLE_OFFSET;
        paintAndRenderWaveform(state);
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
            paintAndRenderWaveform(state);
        }
    }
    else if (event->key.scancode == SDL_SCANCODE_W)
    {
        if (state->samplesPerPixel > 0.01)
        {
            state->samplesPerPixel /= 2.f;
            paintAndRenderWaveform(state);
        }
    }
    else if (event->key.scancode == SDL_SCANCODE_E)
    {
        state->verticalZoom -= 0.3 * multiplier;

        if (state->verticalZoom < 1)
        {
            state->verticalZoom = 1;
        }
        
        paintAndRenderWaveform(state);
    }
    else if (event->key.scancode == SDL_SCANCODE_R)
    {
            state->verticalZoom += 0.3 * multiplier;

            paintAndRenderWaveform(state);
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

        paintAndRenderWaveform(state);
    }
    else if (event->key.scancode == SDL_SCANCODE_RIGHT)
    {
        if (state->sampleOffset >= state->sampleDataL.size())
        {
            return;
        }

        state->sampleOffset += std::max(state->samplesPerPixel, 1.0) * multiplier;
        paintAndRenderWaveform(state);
    }
}

