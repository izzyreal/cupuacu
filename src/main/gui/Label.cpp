#include "Label.hpp"
#include "../State.hpp"
#include "LabelPlanning.hpp"
#include "text.hpp"

#include <cmath>

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

void Label::updateTexture(SDL_Renderer *renderer, const int availableWidth)
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
    const std::string renderedText =
        overflowMode == TextOverflowMode::Ellipsis
            ? ellipsizeTextToWidth(
                  text, availableWidth,
                  [fontPointSize](const std::string &value)
                  {
                      return cupuacu::gui::measureText(value, fontPointSize)
                          .first;
                  })
            : text;
    textTruncated = renderedText != text;
    if (renderedText.empty())
    {
        cachedW = 0;
        cachedH = 0;
        cachedText = text;
        cachedRenderedText = renderedText;
        cachedPointSize = fontPointSize;
        cachedOpacity = opacity;
        cachedAvailableWidth = availableWidth;
        cachedOverflowMode = overflowMode;
        return;
    }

    SDL_Surface *surf = TTF_RenderText_Blended(
        font, renderedText.c_str(), renderedText.size(), textColor);
    if (!surf)
    {
        return;
    }

    cachedW = surf->w;
    cachedH = surf->h;
    cachedTexture = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_DestroySurface(surf);

    cachedText = text;
    cachedRenderedText = renderedText;
    cachedPointSize = fontPointSize;
    cachedOpacity = opacity;
    cachedAvailableWidth = availableWidth;
    cachedOverflowMode = overflowMode;
}

void Label::onDraw(SDL_Renderer *renderer)
{
    const uint8_t fontPointSize = getEffectiveFontSize();
    const float marginScaled = margin * getCanvasSpaceScale(state);
    const int availableWidth = std::max(
        0, static_cast<int>(std::floor(getLocalBoundsF().w - marginScaled * 2)));

    // Rebuild texture if needed
    if (shouldRebuildLabelTexture(cachedTexture, cachedText, text,
                                  cachedPointSize, fontPointSize,
                                  cachedOpacity, opacity) ||
        cachedAvailableWidth != availableWidth ||
        cachedOverflowMode != overflowMode)
    {
        updateTexture(renderer, availableWidth);
    }

    if (!cachedTexture)
    {
        return;
    }

    const SDL_FRect contentRect = planLabelContentRect(
        getLocalBoundsF(), marginScaled, centerVertically, cachedH);
    const SDL_FRect destRect = planLabelDestRect(
        contentRect, cachedW, cachedH, centerHorizontally, state->pixelScale);
    SDL_RenderTexture(renderer, cachedTexture, nullptr, &destRect);
}
