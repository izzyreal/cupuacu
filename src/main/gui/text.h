#pragma once
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3/SDL.h>

#include "../ResourceUtil.hpp"

#include <functional>

static TTF_Font* getFont(const uint8_t pointSize)
{
    auto fontData = get_resource_data("Inter_18pt-Regular.ttf");
    auto fontIo = SDL_IOFromMem(fontData.data(), fontData.size());
    TTF_Font* font = TTF_OpenFontIO(fontIo, false, pointSize);

    if (!font)
    {
        printf("Problem opening TTF font\n");
        return nullptr;
    }

    return font;
}

static std::pair<int, int> measureText(const std::string text, const uint8_t pointSize)
{
    auto font = getFont(pointSize);
    int textW = 0, textH = 0;
    if (!TTF_GetStringSize(font, text.c_str(), text.size(), &textW, &textH))
    {
        printf("Problem sizing text: %s\n", SDL_GetError());
        TTF_CloseFont(font);
        return {0,0};
    }

    TTF_CloseFont(font);

    return {textW, textH};
}

const std::function<void(
        SDL_Renderer*,
        const std::string&,
        const uint8_t,
        const SDL_FRect&,   // dest rect (component bounds)
        bool                // shouldCenterHorizontally
        )> renderText = [](
            SDL_Renderer *renderer,
            const std::string &text,
            const uint8_t pointSize,
            const SDL_FRect &destRect,
            bool shouldCenterHorizontally)
{
    SDL_Color textColor = {255, 255, 255, 255};

    SDL_Texture *canvas = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, nullptr);

    auto font = getFont(pointSize);
    auto [textW, textH] = measureText(text, pointSize);

    SDL_Surface* textSurface = TTF_RenderText_Blended(font, text.c_str(), text.size(), textColor);
    SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);

    float x = destRect.x;
    if (shouldCenterHorizontally)
        x = destRect.x + (destRect.w - textW) * 0.5f;

    SDL_FRect textDestRect = { x, destRect.y, (float)textW, (float)textH };

    SDL_SetRenderTarget(renderer, canvas);
    SDL_RenderTexture(renderer, textTexture, nullptr, &textDestRect);

    SDL_DestroySurface(textSurface);
    SDL_DestroyTexture(textTexture);
    TTF_CloseFont(font);
};

