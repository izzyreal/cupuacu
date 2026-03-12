#pragma once

#include <SDL3/SDL.h>

namespace cupuacu::gui
{
    struct WindowResizePlan
    {
        bool valid = false;
        bool requiresWindowResize = false;
        bool restoreFromMaximized = false;
        int targetWindowWidth = 0;
        int targetWindowHeight = 0;
    };

    inline SDL_Point planWindowCanvasDimensions(const int pixelWidth,
                                                const int pixelHeight,
                                                const int pixelScale)
    {
        if (pixelWidth <= 0 || pixelHeight <= 0 || pixelScale <= 0)
        {
            return {0, 0};
        }

        return {pixelWidth / pixelScale, pixelHeight / pixelScale};
    }

    inline bool shouldRecreateWindowCanvas(const int currentCanvasWidth,
                                           const int currentCanvasHeight,
                                           const SDL_Point requiredCanvas)
    {
        return requiredCanvas.x > 0 && requiredCanvas.y > 0 &&
               (currentCanvasWidth != requiredCanvas.x ||
                currentCanvasHeight != requiredCanvas.y);
    }

    inline WindowResizePlan planWindowResize(const int windowWidth,
                                             const int windowHeight,
                                             const int pixelScale,
                                             const bool wasMaximized)
    {
        WindowResizePlan plan{};
        if (windowWidth <= 0 || windowHeight <= 0 || pixelScale <= 0)
        {
            return plan;
        }

        plan.valid = true;
        plan.targetWindowWidth = windowWidth / pixelScale * pixelScale;
        plan.targetWindowHeight = windowHeight / pixelScale * pixelScale;
        plan.requiresWindowResize =
            plan.targetWindowWidth != windowWidth ||
            plan.targetWindowHeight != windowHeight;
        plan.restoreFromMaximized =
            plan.requiresWindowResize && wasMaximized;
        return plan;
    }
} // namespace cupuacu::gui
