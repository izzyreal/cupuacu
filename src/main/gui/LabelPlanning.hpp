#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <functional>
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

        x = std::round(x);
        y = std::round(y);

        return {x, y, static_cast<float>(cachedW), static_cast<float>(cachedH)};
    }

    inline std::size_t clampToUtf8CodepointBoundary(const std::string &text,
                                                    std::size_t length)
    {
        length = std::min(length, text.size());
        while (length > 0 && length < text.size() &&
               (static_cast<unsigned char>(text[length]) & 0xc0u) == 0x80u)
        {
            --length;
        }
        return length;
    }

    inline std::string ellipsizeTextToWidth(
        const std::string &text, const int availableWidth,
        const std::function<int(const std::string &)> &measureWidth)
    {
        if (text.empty() || availableWidth <= 0)
        {
            return "";
        }

        if (measureWidth(text) <= availableWidth)
        {
            return text;
        }

        constexpr const char *ellipsis = "...";
        if (measureWidth(ellipsis) > availableWidth)
        {
            return "";
        }

        std::size_t low = 0;
        std::size_t high = text.size();
        while (low < high)
        {
            const std::size_t mid = low + (high - low + 1) / 2;
            const std::size_t prefixLength =
                clampToUtf8CodepointBoundary(text, mid);
            const std::string candidate =
                text.substr(0, prefixLength) + ellipsis;
            if (measureWidth(candidate) <= availableWidth)
            {
                low = mid;
            }
            else
            {
                high = mid - 1;
            }
        }

        return text.substr(0, clampToUtf8CodepointBoundary(text, low)) +
               ellipsis;
    }
} // namespace cupuacu::gui
