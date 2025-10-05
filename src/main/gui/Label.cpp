#include "Label.h"
#include "../State.h"
#include "text.h"

using namespace cupuacu::gui;

Label::Label(cupuacu::State* state,
             const std::string& textToUse)
    : Component(state, "Label: " + textToUse),
      text(textToUse)
{
}

void Label::setOpacity(const uint8_t opacityToUse)
{
    opacity = opacityToUse;
    setDirty();
}

Label::~Label() {
    if (cachedTexture) {
        SDL_DestroyTexture(cachedTexture);
        cachedTexture = nullptr;
    }
}

void Label::updateTexture(SDL_Renderer* renderer) {
    if (cachedTexture) {
        SDL_DestroyTexture(cachedTexture);
        cachedTexture = nullptr;
    }

    const uint8_t fontPointSize = getEffectiveFontSize();
    auto font = getFont(fontPointSize);
    if (!font) return;

    SDL_Color textColor = {255, 255, 255, opacity};
    SDL_Surface* surf = TTF_RenderText_Blended(font, text.c_str(), text.size(), textColor);
    if (!surf) return;

    cachedW = surf->w;
    cachedH = surf->h;
    cachedTexture = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_DestroySurface(surf);

    cachedText = text;
    cachedPointSize = fontPointSize;
    cachedOpacity = opacity;
}

void Label::onDraw(SDL_Renderer* renderer)
{
    const uint8_t fontPointSize = (float) pointSize / state->pixelScale;

    // Rebuild texture if needed
    if (!cachedTexture || cachedText != text || cachedPointSize != fontPointSize || opacity != cachedOpacity) {
        updateTexture(renderer);
    }

    if (!cachedTexture) return;

    SDL_FRect rect = getLocalBoundsF();

    float marginScaled = margin / state->pixelScale;
    rect.x += marginScaled;
    rect.y += marginScaled;
    rect.w -= marginScaled * 2;
    rect.h -= marginScaled * 2;

    // Vertical centering
    if (centerVertically) {
        rect.y = rect.y + (rect.h - cachedH) * 0.5f;
        rect.h = (float)cachedH;
    }

    float x = rect.x;
    if (centerHorizontally) {
        float centeredX = rect.x + (rect.w - cachedW) * 0.5f;
        x = std::max(centeredX, rect.x);
    }

    SDL_FRect destRect = { x, rect.y, (float)cachedW, (float)cachedH };
    SDL_RenderTexture(renderer, cachedTexture, nullptr, &destRect);
}

