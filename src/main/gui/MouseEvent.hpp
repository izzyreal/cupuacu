#pragma once

#include <cstdint>
#include <cmath>

#include <SDL3/SDL.h>

namespace cupuacu::gui
{
    enum MouseEventType
    {
        MOVE,
        DOWN,
        UP,
        WHEEL
    };

    struct MouseButtonState
    {
        const bool left;
        const bool middle;
        const bool right;
    };

    struct MouseEvent
    {
        const MouseEventType type;
        const int32_t mouseXi;
        const int32_t mouseYi;
        const float mouseXf;
        const float mouseYf;
        const float mouseRelX;
        const float mouseRelY;
        const MouseButtonState buttonState;
        const uint8_t numClicks;
        const float wheelX = 0.0f;
        const float wheelY = 0.0f;
    };

    static MouseEvent withNewCoordinates(const MouseEvent &evt,
                                         const int32_t newXi,
                                         const int32_t newYi, const float newXf,
                                         const float newYf)
    {
        return MouseEvent{evt.type,      newXi,           newYi,
                          newXf,         newYf,           evt.mouseRelX,
                          evt.mouseRelY, evt.buttonState, evt.numClicks,
                          evt.wheelX,    evt.wheelY};
    }
} // namespace cupuacu::gui
