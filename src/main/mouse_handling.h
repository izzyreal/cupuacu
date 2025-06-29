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
    switch (event->type)
    {
        case SDL_EVENT_MOUSE_MOTION:
        {
            paintWaveform(state);

            int winW, winH;
            float texW, texH;

            SDL_GetWindowSize(window, &winW, &winH);
            SDL_GetTextureSize(canvas, &texW, &texH);

            float scaleX = (float)texW / winW;
            float scaledX = event->motion.x * scaleX;

            SDL_SetRenderTarget(renderer, canvas);
            SDL_SetRenderDrawColor(renderer, 6, 128, 255, 128);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_RenderLine(renderer, (int)scaledX, 0, (int)scaledX, texH);
            SDL_SetRenderTarget(renderer, NULL);
            renderCanvasToWindow(state);
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            break;
        default:
            break;
    }

}

