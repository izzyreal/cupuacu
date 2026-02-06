#pragma once
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3/SDL.h>

#include "../ResourceUtil.hpp"

#include <functional>
#include <map>
#include <string>

namespace cupuacu::gui
{

    // Font cache to store TTF_Font instances by point size
    static std::map<uint8_t, TTF_Font *> &getFontCache()
    {
        static std::map<uint8_t, TTF_Font *> fontCache;
        return fontCache;
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
        const auto it = cache.find(pointSize);
        if (it != cache.end())
        {
            return it->second; // Return cached font
        }

        auto &fontData = getFontData();
        const auto fontIo = SDL_IOFromMem(fontData.data(), fontData.size());
        TTF_Font *font = TTF_OpenFontIO(fontIo, false, pointSize);

        if (!font)
        {
            printf("Problem opening TTF font for point size %u: %s\n",
                   pointSize, SDL_GetError());
            return nullptr;
        }

        cache[pointSize] = font; // Store in cache
        return font;
    }

    // Clean up all cached fonts (call at program exit)
    static void cleanupFonts()
    {
        auto &cache = getFontCache();
        for (auto &[pointSize, font] : cache)
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
                        bool shouldCenterHorizontally)
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

        float x = destRect.x;
        if (shouldCenterHorizontally)
        {
            const float centeredX = destRect.x + (destRect.w - textW) * 0.5f;
            x = std::max(centeredX,
                         destRect.x); // Prevent left overflow; fallback to
                                      // left-align if needed
        }

        const SDL_FRect textDestRect = {x, destRect.y, (float)textW, (float)textH};

        SDL_SetRenderTarget(renderer, canvas);
        SDL_RenderTexture(renderer, textTexture, nullptr, &textDestRect);

        SDL_DestroySurface(textSurface);
        SDL_DestroyTexture(textTexture);
    };
} // namespace cupuacu::gui
