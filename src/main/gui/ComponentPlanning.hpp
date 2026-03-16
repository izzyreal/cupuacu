#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace cupuacu::gui
{
    inline void appendVisibleDirtySubtreeRects(
        const SDL_Rect rootRect, const bool rootVisible,
        const std::vector<std::pair<SDL_Rect, bool>> &visibleDescendants,
        std::vector<SDL_Rect> &outRects)
    {
        if (!rootVisible)
        {
            return;
        }
        outRects.push_back(rootRect);
        for (const auto &[rect, visible] : visibleDescendants)
        {
            if (visible)
            {
                outRects.push_back(rect);
            }
        }
    }

    inline bool shouldDirtyParentAfterBoundsChange(const SDL_Rect oldBounds,
                                                   const SDL_Rect newBounds,
                                                   const bool hasParent)
    {
        if (!hasParent)
        {
            return false;
        }

        SDL_Rect unionBounds{};
        SDL_GetRectUnion(&oldBounds, &newBounds, &unionBounds);
        return !SDL_RectsEqual(&unionBounds, &newBounds);
    }

    inline std::size_t reorderIndexForSendToBack(const std::size_t index,
                                                 const std::size_t childCount)
    {
        if (childCount == 0 || index >= childCount)
        {
            return index;
        }
        return 0;
    }

    inline std::size_t reorderIndexForBringToFront(const std::size_t index,
                                                   const std::size_t childCount)
    {
        if (childCount == 0 || index >= childCount)
        {
            return index;
        }
        return childCount - 1;
    }

    inline bool containsAbsoluteCoordinateWithOptionalParentClipping(
        const SDL_Rect absoluteBounds, const bool parentClippingEnabled,
        const SDL_Rect parentBounds, const int x, const int y)
    {
        const SDL_Point point{x, y};
        if (!SDL_PointInRect(&point, &absoluteBounds))
        {
            return false;
        }
        if (!parentClippingEnabled)
        {
            return true;
        }
        return SDL_PointInRect(&point, &parentBounds);
    }
} // namespace cupuacu::gui
