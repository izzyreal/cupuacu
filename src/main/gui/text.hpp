#pragma once
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3/SDL.h>

#include "../ResourceUtil.hpp"
#include "TextPlanning.hpp"

#include <functional>
#include <map>
#include <cmath>
#include <string>
#include <utility>

namespace cupuacu::gui
{
    static void cleanupFonts();

    static float &getFontDisplayScale()
    {
        static float fontDisplayScale = 1.0f;
        return fontDisplayScale;
    }

    inline int getFontDpi()
    {
        const float safeScale = std::max(1.0f, getFontDisplayScale());
        return std::max(1, static_cast<int>(std::lround(72.0f * safeScale)));
    }

    inline void setFontDisplayScale(const float scale)
    {
        const float safeScale = std::max(1.0f, scale);
        if (std::fabs(getFontDisplayScale() - safeScale) < 0.01f)
        {
            return;
        }
        cleanupFonts();
        getFontDisplayScale() = safeScale;
    }

    // Font cache to store TTF_Font instances by point size and DPI
    static std::map<std::pair<uint8_t, int>, TTF_Font *> &getFontCache()
    {
        static std::map<std::pair<uint8_t, int>, TTF_Font *> fontCache;
        return fontCache;
    }

    inline bool hasCachedFontForPointSize(const uint8_t pointSize)
    {
        return getFontCache().find({pointSize, getFontDpi()}) !=
               getFontCache().end();
    }

    // Store font data once
    static std::string &getFontData()
    {
        static std::string fontData =
            get_resource_data("Inter_18pt-Regular.ttf");
        return fontData;
    }

    static TTF_Font *getFont(const uint8_t pointSize)
    {
        auto &cache = getFontCache();
        const std::pair<uint8_t, int> cacheKey{pointSize, getFontDpi()};
        const auto it = cache.find(cacheKey);
        if (it != cache.end())
        {
            return it->second; // Return cached font
        }

        auto &fontData = getFontData();
        const auto fontIo = SDL_IOFromMem(fontData.data(), fontData.size());
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, TTF_PROP_FONT_CREATE_IOSTREAM_POINTER,
                               fontIo);
        SDL_SetBooleanProperty(props,
                               TTF_PROP_FONT_CREATE_IOSTREAM_AUTOCLOSE_BOOLEAN,
                               false);
        SDL_SetFloatProperty(props, TTF_PROP_FONT_CREATE_SIZE_FLOAT, pointSize);
        SDL_SetNumberProperty(props,
                              TTF_PROP_FONT_CREATE_HORIZONTAL_DPI_NUMBER,
                              getFontDpi());
        SDL_SetNumberProperty(props,
                              TTF_PROP_FONT_CREATE_VERTICAL_DPI_NUMBER,
                              getFontDpi());
        TTF_Font *font = TTF_OpenFontWithProperties(props);
        SDL_DestroyProperties(props);

        if (!font)
        {
            printf("Problem opening TTF font for point size %u at dpi %d: %s\n",
                   pointSize, getFontDpi(), SDL_GetError());
            return nullptr;
        }

        cache[cacheKey] = font; // Store in cache
        return font;
    }

    // Clean up all cached fonts (call at program exit)
    static void cleanupFonts()
    {
        auto &cache = getFontCache();
        for (auto &[key, font] : cache)
        {
            TTF_CloseFont(font);
        }
        cache.clear();
    }

    static std::pair<int, int> measureText(const std::string &text,
                                           const uint8_t pointSize)
    {
        const auto font = getFont(pointSize);
        if (!font)
        {
            return {0, 0};
        }
        int textW = 0, textH = 0;
        if (!TTF_GetStringSize(font, text.c_str(), text.size(), &textW, &textH))
        {
            printf("Problem sizing text: %s\n", SDL_GetError());
            return {0, 0};
        }
        return {textW, textH};
    }

    const std::function<void(SDL_Renderer *, const std::string &, const uint8_t,
                             const SDL_FRect &, // dest rect (component bounds)
                             bool               // shouldCenterHorizontally
                             )>
        renderText = [](SDL_Renderer *renderer, const std::string &text,
                        const uint8_t pointSize, const SDL_FRect &destRect,
                        const bool shouldCenterHorizontally)
    {
        constexpr SDL_Color textColor = {255, 255, 255, 255};

        SDL_Texture *canvas = SDL_GetRenderTarget(renderer);
        SDL_SetRenderTarget(renderer, nullptr);

        const auto font = getFont(pointSize);
        if (!font)
        {
            SDL_SetRenderTarget(renderer, canvas);
            return;
        }
        auto [textW, textH] = measureText(text, pointSize);

        SDL_Surface *textSurface =
            TTF_RenderText_Blended(font, text.c_str(), text.size(), textColor);
        if (!textSurface)
        {
            printf("Problem rendering text: %s\n", SDL_GetError());
            SDL_SetRenderTarget(renderer, canvas);
            return;
        }
        SDL_Texture *textTexture =
            SDL_CreateTextureFromSurface(renderer, textSurface);
        if (!textTexture)
        {
            printf("Problem creating texture: %s\n", SDL_GetError());
            SDL_DestroySurface(textSurface);
            SDL_SetRenderTarget(renderer, canvas);
            return;
        }

        const float x =
            planTextXPosition(destRect, textW, shouldCenterHorizontally);

        const SDL_FRect textDestRect = {x, std::round(destRect.y), (float)textW,
                                        (float)textH};

        SDL_SetRenderTarget(renderer, canvas);
        SDL_RenderTexture(renderer, textTexture, nullptr, &textDestRect);

        SDL_DestroySurface(textSurface);
        SDL_DestroyTexture(textTexture);
    };
} // namespace cupuacu::gui
