#pragma once

#include <SDL3/SDL.h>

#include <algorithm>

namespace cupuacu::gui
{
    struct RoundedRectGeometry
    {
        float radius = 0.0f;
        float x0 = 0.0f;
        float y0 = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;
    };

    inline float clampRoundedRectRadius(const SDL_FRect &rect, float radius)
    {
        if (radius <= 0.0f)
        {
            return 0.0f;
        }
        return std::min(radius, std::min(rect.w, rect.h) / 2.0f);
    }

    inline RoundedRectGeometry planRoundedRectGeometry(const SDL_FRect &rect,
                                                       const float radius)
    {
        const float clampedRadius = clampRoundedRectRadius(rect, radius);
        return {clampedRadius, rect.x, rect.y, rect.x + rect.w - 1.0f,
                rect.y + rect.h - 1.0f};
    }

    inline SDL_FRect planRoundedRectCore(const SDL_FRect &rect,
                                         const float radius)
    {
        return {rect.x + radius, rect.y, rect.w - 2 * radius, rect.h};
    }

    inline SDL_FRect planRoundedRectVerticalCore(const SDL_FRect &rect,
                                                 const float radius)
    {
        return {rect.x, rect.y + radius, rect.w, rect.h - 2 * radius};
    }
} // namespace cupuacu::gui
