#include <SDL3/SDL.h>

#include "CupuacuState.h"

#include <functional>

static void handleMouseEvent(
        SDL_Event *event,
        SDL_Renderer *renderer,
        SDL_Texture *canvas,
        SDL_Window *window,
        const std::function<void(CupuacuState*)> &paintWaveform,
        const std::function<void(CupuacuState*)> &renderCanvasToWindow,
        CupuacuState *state)
{
    int winW, winH;
    float texW, texH;
    const auto samplesPerPixel = state->samplesPerPixel;
    auto sampleOffset = state->sampleOffset;
    SDL_GetWindowSize(window, &winW, &winH);
    SDL_GetTextureSize(canvas, &texW, &texH);
    float scaleX = (float)texW / winW;
    
    switch (event->type)
    {
        case SDL_EVENT_MOUSE_MOTION:
        {
            if (event->motion.state & SDL_BUTTON_LMASK)
            {
                if (event->motion.x > winW || event->motion.x < 0)
                {
                    const auto diff = event->motion.x < 0 ? event->motion.x : event->motion.x - winW;
                    const auto samplesToScroll = diff * scaleX * samplesPerPixel;
                    state->sampleOffset += samplesToScroll;
                    sampleOffset = state->sampleOffset;
                }

                float scaledX = event->motion.x <= 0 ? 0 : event->motion.x * scaleX;
                state->selectionEndSample = sampleOffset + (uint64_t)(scaledX * samplesPerPixel);
                paintWaveform(state);
                renderCanvasToWindow(state);
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        {
            if (event->button.button == SDL_BUTTON_LEFT)
            {
                float scaledX = event->button.x * scaleX;
                state->selectionStartSample = sampleOffset + (uint64_t)(scaledX * samplesPerPixel);
                state->selectionEndSample = state->selectionStartSample;
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            if (event->button.button == SDL_BUTTON_LEFT)
            {
                float scaledX = event->button.x * scaleX;
                state->selectionEndSample = state->sampleOffset + (uint64_t)(scaledX * state->samplesPerPixel);

                if (state->selectionEndSample < state->selectionStartSample)
                {
                    uint64_t temp = state->selectionStartSample;
                    state->selectionStartSample = state->selectionEndSample;
                    state->selectionEndSample = temp;
                }
                paintWaveform(state);
                renderCanvasToWindow(state);
            }
            break;
        }
        default:
            break;
    }
}
