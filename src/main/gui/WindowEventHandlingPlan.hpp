#pragma once

#include <SDL3/SDL.h>

namespace cupuacu::gui
{
    struct WindowEventHandlingPlan
    {
        bool handled = false;
        bool markMaximized = false;
        bool handleResize = false;
        bool renderFrame = false;
        bool invokeOnClose = false;
        bool closeWindow = false;
        bool forwardAsMouse = false;
    };

    inline WindowEventHandlingPlan planWindowEventHandling(
        const Uint32 eventType, const bool hasRootComponent)
    {
        WindowEventHandlingPlan plan{};

        switch (eventType)
        {
            case SDL_EVENT_WINDOW_MAXIMIZED:
                plan.handled = true;
                plan.markMaximized = true;
                return plan;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                plan.handled = true;
                plan.handleResize = true;
                return plan;
            case SDL_EVENT_WINDOW_EXPOSED:
                plan.handled = true;
                plan.renderFrame = true;
                return plan;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                plan.handled = true;
                plan.invokeOnClose = true;
                plan.closeWindow = true;
                return plan;
            case SDL_EVENT_MOUSE_MOTION:
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            case SDL_EVENT_MOUSE_WHEEL:
                plan.handled = hasRootComponent;
                plan.forwardAsMouse = hasRootComponent;
                return plan;
            default:
                return plan;
        }
    }
} // namespace cupuacu::gui
