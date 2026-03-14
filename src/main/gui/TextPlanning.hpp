#pragma once

#include <algorithm>
#include <cmath>
namespace cupuacu::gui
{
    inline float planTextXPosition(const SDL_FRect &destRect, const int textW,
                                   const bool shouldCenterHorizontally)
    {
        if (!shouldCenterHorizontally)
        {
            return destRect.x;
        }

        const float centeredX = destRect.x + (destRect.w - textW) * 0.5f;
        return std::round(std::max(centeredX, destRect.x));
    }
} // namespace cupuacu::gui
