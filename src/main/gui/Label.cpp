#include "Label.hpp"
#include "../State.hpp"
#include "LabelPlanning.hpp"
#include "text.hpp"

using namespace cupuacu::gui;

Label::Label(State *state, const std::string &textToUse)
    : Component(state, "Label: " + textToUse), text(textToUse)
{
}

void Label::setOpacity(const uint8_t opacityToUse)
{
    opacity = opacityToUse;
    setDirty();
}

Label::~Label()
{
    if (cachedTexture)
    {
        SDL_DestroyTexture(cachedTexture);
        cachedTexture = nullptr;
    }
}

void Label::updateTexture(SDL_Renderer *renderer)
{
    if (cachedTexture)
    {
        SDL_DestroyTexture(cachedTexture);
        cachedTexture = nullptr;
    }

    const uint8_t fontPointSize = getEffectiveFontSize();
    const auto font = getFont(fontPointSize);
    if (!font)
    {
        return;
    }

    const SDL_Color textColor = {255, 255, 255, opacity};
    SDL_Surface *surf =
        TTF_RenderText_Blended(font, text.c_str(), text.size(), textColor);
    if (!surf)
    {
        return;
    }

    cachedW = surf->w;
    cachedH = surf->h;
    cachedTexture = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_DestroySurface(surf);

    cachedText = text;
    cachedPointSize = fontPointSize;
    cachedOpacity = opacity;
}

void Label::onDraw(SDL_Renderer *renderer)
{
    const uint8_t fontPointSize = getEffectiveFontSize();

    // Rebuild texture if needed
    if (shouldRebuildLabelTexture(cachedTexture, cachedText, text,
                                  cachedPointSize, fontPointSize,
                                  cachedOpacity, opacity))
    {
        updateTexture(renderer);
    }

    if (!cachedTexture)
    {
        return;
    }

    const float marginScaled = margin * getCanvasSpaceScale(state);
    const SDL_FRect contentRect = planLabelContentRect(
        getLocalBoundsF(), marginScaled, centerVertically, cachedH);
    const SDL_FRect destRect = planLabelDestRect(
        contentRect, cachedW, cachedH, centerHorizontally, state->pixelScale);
    SDL_RenderTexture(renderer, cachedTexture, nullptr, &destRect);
}
