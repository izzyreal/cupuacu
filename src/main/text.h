#include <SDL3_ttf/SDL_ttf.h>

#include <SDL3/SDL.h>

#include "ResourceUtil.hpp"

#include <functional>

const std::function<void(SDL_Renderer*, SDL_Texture*)> renderText = [](SDL_Renderer *renderer, SDL_Texture *canvas)
{
    SDL_Color textColor = {255, 255, 255, 255};

    auto fontData = get_resource_data("Inter_18pt-Regular.ttf");

    auto fontIo = SDL_IOFromMem(&fontData[0], fontData.size());

    TTF_Font* font = TTF_OpenFontIO(fontIo, false, 24);
    
    if (!font)
    {
        printf("Problem opening TTF font\n");
        return;
    }

    SDL_Surface* textSurface = TTF_RenderText_Blended(font, "Cupuacu!", 8, textColor);
    SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
    SDL_FRect textDestRect = {0, 0, static_cast<float>(textSurface->w), static_cast<float>(textSurface->h)};
    SDL_SetRenderTarget(renderer, canvas);
    SDL_RenderTexture(renderer, textTexture, nullptr, &textDestRect);
    SDL_DestroySurface(textSurface);
    SDL_DestroyTexture(textTexture);
    TTF_CloseFont(font);
    SDL_SetRenderTarget(renderer, nullptr);
};

