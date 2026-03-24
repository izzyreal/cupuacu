#pragma once

#include "MouseEvent.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <optional>

namespace cupuacu::gui
{
    struct WindowMouseEventDraft
    {
        bool valid = false;
        MouseEventType type = MOVE;
        float xf = 0.0f;
        float yf = 0.0f;
        float relx = 0.0f;
        float rely = 0.0f;
        bool left = false;
        bool middle = false;
        bool right = false;
        uint8_t clicks = 0;
        float wheelX = 0.0f;
        float wheelY = 0.0f;
    };

    inline std::optional<SDL_WindowID> getWindowEventWindowId(
        const SDL_Event &event)
    {
        switch (event.type)
        {
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_MAXIMIZED:
            case SDL_EVENT_WINDOW_EXPOSED:
            case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                return event.window.windowID;
            case SDL_EVENT_MOUSE_MOTION:
                return event.motion.windowID;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                return event.button.windowID;
            case SDL_EVENT_MOUSE_WHEEL:
                return event.wheel.windowID;
            case SDL_EVENT_KEY_DOWN:
                return event.key.windowID;
            case SDL_EVENT_TEXT_INPUT:
                return event.text.windowID;
            default:
                return std::nullopt;
        }
    }

    inline WindowMouseEventDraft draftWindowMouseEvent(const SDL_Event &event)
    {
        WindowMouseEventDraft draft{};

        if (event.type == SDL_EVENT_MOUSE_MOTION)
        {
            draft.valid = true;
            draft.type = MOVE;
            draft.xf = event.motion.x;
            draft.yf = event.motion.y;
            draft.relx = event.motion.xrel;
            draft.rely = event.motion.yrel;
            draft.left = (event.motion.state & SDL_BUTTON_LMASK) != 0;
            draft.middle = (event.motion.state & SDL_BUTTON_MMASK) != 0;
            draft.right = (event.motion.state & SDL_BUTTON_RMASK) != 0;
        }
        else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                 event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        {
            draft.valid = true;
            draft.type = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? DOWN : UP;
            draft.xf = event.button.x;
            draft.yf = event.button.y;
            draft.left = event.button.button == SDL_BUTTON_LEFT;
            draft.middle = event.button.button == SDL_BUTTON_MIDDLE;
            draft.right = event.button.button == SDL_BUTTON_RIGHT;
            draft.clicks = event.button.clicks;
        }
        else if (event.type == SDL_EVENT_MOUSE_WHEEL)
        {
            draft.valid = true;
            draft.type = WHEEL;
            draft.xf = event.wheel.mouse_x;
            draft.yf = event.wheel.mouse_y;
            draft.wheelX = event.wheel.x;
            draft.wheelY = event.wheel.y;
        }

        return draft;
    }

    inline void scaleWindowMouseEventDraft(WindowMouseEventDraft &draft,
                                           const float canvasWidth,
                                           const float canvasHeight,
                                           const int windowWidth,
                                           const int windowHeight)
    {
        if (!draft.valid || canvasWidth <= 0.0f || canvasHeight <= 0.0f ||
            windowWidth <= 0 || windowHeight <= 0)
        {
            return;
        }

        const float scaleX = canvasWidth / static_cast<float>(windowWidth);
        const float scaleY = canvasHeight / static_cast<float>(windowHeight);
        draft.xf *= scaleX;
        draft.yf *= scaleY;
        draft.relx *= scaleX;
        draft.rely *= scaleY;
    }

    inline MouseEvent finalizeWindowMouseEvent(
        const WindowMouseEventDraft &draft)
    {
        const int32_t xi = static_cast<int32_t>(std::floor(draft.xf));
        const int32_t yi = static_cast<int32_t>(std::floor(draft.yf));
        const MouseButtonState bs{draft.left, draft.middle, draft.right};
        return MouseEvent{draft.type, xi,       yi,       draft.xf,  draft.yf,
                          draft.relx, draft.rely, bs,       draft.clicks,
                          draft.wheelX, draft.wheelY};
    }
} // namespace cupuacu::gui
