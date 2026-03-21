#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cupuacu::gui
{
    inline SDL_Rect mapCanvasRectToScreenRect(const SDL_Rect &canvasRect,
                                              const SDL_Rect &parentWindowBounds,
                                              const SDL_FPoint &canvasSize)
    {
        if (canvasRect.w <= 0 || canvasRect.h <= 0 ||
            parentWindowBounds.w <= 0 || parentWindowBounds.h <= 0 ||
            canvasSize.x <= 0.0f || canvasSize.y <= 0.0f)
        {
            return SDL_Rect{0, 0, 0, 0};
        }

        const float scaleX =
            static_cast<float>(parentWindowBounds.w) / canvasSize.x;
        const float scaleY =
            static_cast<float>(parentWindowBounds.h) / canvasSize.y;
        if (scaleX <= 0.0f || scaleY <= 0.0f)
        {
            return SDL_Rect{0, 0, 0, 0};
        }

        return SDL_Rect{
            parentWindowBounds.x +
                static_cast<int>(std::lround(canvasRect.x * scaleX)),
            parentWindowBounds.y +
                static_cast<int>(std::lround(canvasRect.y * scaleY)),
            std::max(1, static_cast<int>(std::lround(canvasRect.w * scaleX))),
            std::max(1, static_cast<int>(std::lround(canvasRect.h * scaleY)))};
    }

    struct TooltipPopupPlacement
    {
        bool valid = false;
        int popupX = 0;
        int popupY = 0;
        int offsetX = 0;
        int offsetY = 0;
    };

    struct TooltipPopupGeometry
    {
        bool valid = false;
        int logicalWidth = 0;
        int logicalHeight = 0;
        int canvasWidth = 0;
        int canvasHeight = 0;
        int gapLogical = 0;
        int renderPaddingPx = 0;
    };

    inline TooltipPopupGeometry planTooltipPopupGeometry(
        const int textWidthPx, const int textHeightPx, const float displayScale,
        const int paddingCanvasPx, const int gapCanvasPx,
        const uint8_t pixelScale, const int maxDisplayLogicalWidth,
        const int maxDisplayLogicalHeight)
    {
        TooltipPopupGeometry plan;
        const int safePixelScale =
            std::max(1, static_cast<int>(pixelScale));
        const float safeDisplayScale = std::max(1.0f, displayScale);
        const int safePadding = std::max(0, paddingCanvasPx);
        const int safeGapCanvas = std::max(0, gapCanvasPx);
        const int maxCanvasWidth = std::max(
            1, static_cast<int>(std::floor(maxDisplayLogicalWidth *
                                           safeDisplayScale / safePixelScale)));
        const int maxCanvasHeight = std::max(
            1, static_cast<int>(std::floor(maxDisplayLogicalHeight *
                                           safeDisplayScale / safePixelScale)));

        if (textWidthPx <= 0 || textHeightPx <= 0 || maxDisplayLogicalWidth <= 0 ||
            maxDisplayLogicalHeight <= 0 || maxCanvasWidth <= 0 ||
            maxCanvasHeight <= 0)
        {
            return plan;
        }

        const int measuredCanvasWidth =
            std::max(1, textWidthPx + safePadding * 2);
        const int measuredCanvasHeight =
            std::max(1, textHeightPx + safePadding * 2);

        plan.valid = true;
        plan.canvasWidth = std::min(measuredCanvasWidth, maxCanvasWidth);
        plan.canvasHeight = std::min(measuredCanvasHeight, maxCanvasHeight);
        plan.logicalWidth = std::max(
            1, static_cast<int>(std::ceil(plan.canvasWidth * safePixelScale /
                                          safeDisplayScale)));
        plan.logicalHeight = std::max(
            1, static_cast<int>(std::ceil(plan.canvasHeight * safePixelScale /
                                          safeDisplayScale)));
        plan.gapLogical = std::max(
            1, static_cast<int>(std::ceil(safeGapCanvas * safePixelScale /
                                          safeDisplayScale)));
        plan.renderPaddingPx = safePadding;
        return plan;
    }

    inline TooltipPopupPlacement planTooltipPopupPlacement(
        const SDL_Rect &parentWindowBounds, const SDL_Rect &anchorBounds,
        const SDL_Rect &displayBounds, const int popupWidth,
        const int popupHeight, const int gap)
    {
        TooltipPopupPlacement plan;
        if (parentWindowBounds.w <= 0 || parentWindowBounds.h <= 0 ||
            anchorBounds.w <= 0 || anchorBounds.h <= 0 || popupWidth <= 0 ||
            popupHeight <= 0 || displayBounds.w <= 0 || displayBounds.h <= 0)
        {
            return plan;
        }

        const int displayRight = displayBounds.x + displayBounds.w;
        const int displayBottom = displayBounds.y + displayBounds.h;

        int popupX = anchorBounds.x + (anchorBounds.w - popupWidth) / 2;
        int popupY = anchorBounds.y + anchorBounds.h + gap;

        if (popupY + popupHeight > displayBottom)
        {
            popupY = anchorBounds.y - popupHeight - gap;
        }

        popupX = std::clamp(popupX, displayBounds.x,
                            displayRight - popupWidth);
        popupY = std::clamp(popupY, displayBounds.y,
                            displayBottom - popupHeight);

        plan.valid = true;
        plan.popupX = popupX;
        plan.popupY = popupY;
        plan.offsetX = popupX - parentWindowBounds.x;
        plan.offsetY = popupY - parentWindowBounds.y;
        return plan;
    }
} // namespace cupuacu::gui
