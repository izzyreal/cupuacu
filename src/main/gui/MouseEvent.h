#pragma once

#include "../CupuacuState.h"

#include <cstdint>
#include <cmath>

#include <SDL3/SDL.h>

enum MouseEventType { MOVE, DOWN, UP };

struct MouseButtonState {
    const bool left;
    const bool middle;
    const bool right;
};

struct MouseEvent {
    const MouseEventType type;
    const int32_t mouseXi;
    const int32_t mouseYi;
    const float mouseXf;
    const float mouseYf;
    const float mouseRelX;
    const float mouseRelY;
    const MouseButtonState buttonState;
    const uint8_t numClicks;
};

static void scaleMouseCoordinates(const CupuacuState *const state, float &x, float &y)
{
    SDL_FPoint canvasDimensions;
    SDL_GetTextureSize(state->canvas, &canvasDimensions.x, &canvasDimensions.y);

    SDL_Point winDimensions;
    SDL_GetWindowSize(state->window, &winDimensions.x, &winDimensions.y);

    x *= canvasDimensions.x / winDimensions.x;
    y *= canvasDimensions.y / winDimensions.y;
}

static MouseEvent convertFromSDL(const CupuacuState *state, const SDL_Event *event)
{
    MouseEventType type = MOVE;
    float xf = 0.0f, yf = 0.0f;
    int32_t xi = 0, yi = 0;
    float relx = 0.0f, rely = 0.0f;
    bool left = false, middle = false, right = false;
    uint8_t clicks = 0;

    switch (event->type) {
    case SDL_EVENT_MOUSE_MOTION: {
        type = MOVE;
        xf = event->motion.x;
        yf = event->motion.y;
        relx = event->motion.xrel;
        rely = event->motion.yrel;

        scaleMouseCoordinates(state, xf, yf);
        scaleMouseCoordinates(state, relx, rely);

        xi = static_cast<int32_t>(std::floor(xf));
        yi = static_cast<int32_t>(std::floor(yf));

        left   = (event->motion.state & SDL_BUTTON_LMASK) != 0;
        middle = (event->motion.state & SDL_BUTTON_MMASK) != 0;
        right  = (event->motion.state & SDL_BUTTON_RMASK) != 0;
        clicks = 0;
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        type = (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) ? DOWN : UP;
        xf = event->button.x;
        yf = event->button.y;

        scaleMouseCoordinates(state, xf, yf);

        xi = static_cast<int32_t>(std::floor(xf));
        yi = static_cast<int32_t>(std::floor(yf));

        relx = 0.0f;
        rely = 0.0f;

        left   = (event->button.button == SDL_BUTTON_LEFT);
        middle = (event->button.button == SDL_BUTTON_MIDDLE);
        right  = (event->button.button == SDL_BUTTON_RIGHT);

        clicks = event->button.clicks;
        break;
    }
    default:
        break;
    }

    MouseButtonState bs{ left, middle, right };

    return MouseEvent{
        type,
        xi,
        yi,
        xf,
        yf,
        relx,
        rely,
        bs,
        clicks
    };
}

static MouseEvent withNewCoordinates(const MouseEvent &evt, int32_t newXi, int32_t newYi,
                                     float newXf, float newYf)
{
    return MouseEvent{
        evt.type,
        newXi,
        newYi,
        newXf,
        newYf,
        evt.mouseRelX,
        evt.mouseRelY,
        evt.buttonState,
        evt.numClicks
    };
}

