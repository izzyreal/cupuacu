#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

namespace cupuacu::gui
{
    struct MainViewMarkerPlacement
    {
        bool visible = false;
        SDL_Rect rect{0, 0, 0, 0};
    };

    struct MainViewSelectionMarkersPlan
    {
        MainViewMarkerPlacement selectionStartTop;
        MainViewMarkerPlacement selectionStartBottom;
        MainViewMarkerPlacement selectionEndTop;
        MainViewMarkerPlacement selectionEndBottom;
        MainViewMarkerPlacement cursorTop;
        MainViewMarkerPlacement cursorBottom;
    };

    inline MainViewMarkerPlacement planTopSelectionStartMarker(
        const int32_t startX, const int borderWidth, const int scrollBarHeight,
        const float triHeight)
    {
        return {true,
                {startX + borderWidth, scrollBarHeight,
                 static_cast<int>(triHeight + 1.0f),
                 static_cast<int>(triHeight)}};
    }

    inline MainViewMarkerPlacement planBottomSelectionStartMarker(
        const int32_t startX, const int borderWidth, const float triHeight)
    {
        return {true,
                {startX + borderWidth, 0, static_cast<int>(triHeight + 1.0f),
                 static_cast<int>(triHeight)}};
    }

    inline MainViewMarkerPlacement planTopSelectionEndMarker(
        const int32_t endX, const int borderWidth, const int scrollBarHeight,
        const float triHeight)
    {
        return {true,
                {endX + borderWidth - static_cast<int>(triHeight),
                 scrollBarHeight, static_cast<int>(triHeight),
                 static_cast<int>(triHeight)}};
    }

    inline MainViewMarkerPlacement planBottomSelectionEndMarker(
        const int32_t endX, const int borderWidth, const float triHeight)
    {
        return {true,
                {endX + borderWidth - static_cast<int>(triHeight), 0,
                 static_cast<int>(triHeight), static_cast<int>(triHeight)}};
    }

    inline MainViewMarkerPlacement
    planTopCursorMarker(const int32_t xPos, const int borderWidth,
                        const int scrollBarHeight, const float halfBase,
                        const float triHeight)
    {
        const int cursorX = xPos + borderWidth;
        return {true,
                {cursorX - static_cast<int>(halfBase) + 1, scrollBarHeight,
                 static_cast<int>(halfBase * 2.0f),
                 static_cast<int>(triHeight)}};
    }

    inline MainViewMarkerPlacement
    planBottomCursorMarker(const int32_t xPos, const int borderWidth,
                           const float halfBase, const float triHeight)
    {
        const int cursorX = xPos + borderWidth;
        return {true,
                {cursorX - static_cast<int>(halfBase) + 1, 0,
                 static_cast<int>(halfBase * 2.0f),
                 static_cast<int>(triHeight)}};
    }
} // namespace cupuacu::gui
