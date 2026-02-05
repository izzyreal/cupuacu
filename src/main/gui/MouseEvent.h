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
        UP
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
    };

    static MouseEvent withNewCoordinates(const MouseEvent &evt, int32_t newXi,
                                         int32_t newYi, float newXf,
                                         float newYf)
    {
        return MouseEvent{evt.type,      newXi,           newYi,
                          newXf,         newYf,           evt.mouseRelX,
                          evt.mouseRelY, evt.buttonState, evt.numClicks};
    }
} // namespace cupuacu::gui
