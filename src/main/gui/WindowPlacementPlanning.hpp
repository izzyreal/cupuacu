#pragma once

#include <SDL3/SDL.h>

#include <optional>
#include <vector>

namespace cupuacu::gui
{
    struct InitialWindowPlacementPlan
    {
        bool usePersistedPosition = false;
        int x = 0;
        int y = 0;
    };

    inline bool rectsIntersect(const SDL_Rect &a, const SDL_Rect &b)
    {
        return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h &&
               b.y < a.y + a.h;
    }

    inline InitialWindowPlacementPlan planInitialWindowPlacement(
        const std::optional<int> persistedX, const std::optional<int> persistedY,
        const int windowWidth, const int windowHeight,
        const std::vector<SDL_Rect> &displayBounds)
    {
        InitialWindowPlacementPlan plan{};
        if (!persistedX.has_value() || !persistedY.has_value() || windowWidth <= 0 ||
            windowHeight <= 0)
        {
            return plan;
        }

        if (displayBounds.empty())
        {
            plan.usePersistedPosition = true;
            plan.x = *persistedX;
            plan.y = *persistedY;
            return plan;
        }

        const SDL_Rect windowRect{*persistedX, *persistedY, windowWidth,
                                  windowHeight};
        for (const auto &displayRect : displayBounds)
        {
            if (rectsIntersect(windowRect, displayRect))
            {
                plan.usePersistedPosition = true;
                plan.x = *persistedX;
                plan.y = *persistedY;
                return plan;
            }
        }

        return plan;
    }
} // namespace cupuacu::gui
