#pragma once

#include <SDL3/SDL.h>

#include <cmath>
#include <string>

namespace cupuacu::gui
{
    inline bool shouldRebuildLabelTexture(const SDL_Texture *cachedTexture,
                                          const std::string &cachedText,
                                          const std::string &text,
                                          const int cachedPointSize,
                                          const uint8_t pointSize,
                                          const int cachedOpacity,
                                          const uint8_t opacity)
    {
        return !cachedTexture || cachedText != text ||
               cachedPointSize != pointSize || cachedOpacity != opacity;
    }

    inline SDL_FRect planLabelContentRect(const SDL_FRect bounds,
                                          const float marginScaled,
                                          const bool centerVertically,
                                          const int cachedH)
    {
        SDL_FRect rect = bounds;
        rect.x += marginScaled;
        rect.y += marginScaled;
        rect.w -= marginScaled * 2;
        rect.h -= marginScaled * 2;

        if (centerVertically)
        {
            rect.y = rect.y + (rect.h - cachedH) * 0.5f;
            rect.h = static_cast<float>(cachedH);
        }

        return rect;
    }

    inline SDL_FRect planLabelDestRect(const SDL_FRect contentRect,
                                       const int cachedW, const int cachedH,
                                       const bool centerHorizontally,
                                       const uint8_t pixelScale)
    {
        float x = contentRect.x;
        float y = contentRect.y;
        if (centerHorizontally)
        {
            const float centeredX =
                contentRect.x + (contentRect.w - cachedW) * 0.5f;
            x = std::max(centeredX, contentRect.x);
        }

        if (pixelScale > 1)
        {
            x = std::round(x);
            y = std::floor(y);
        }

        return {x, y, static_cast<float>(cachedW), static_cast<float>(cachedH)};
    }
} // namespace cupuacu::gui
