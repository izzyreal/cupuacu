#include "Label.h"
#include "../CupuacuState.h"

#include "text.h"

Label::Label(CupuacuState* state,
             const std::string& textToUse)
    : Component(state, "Label: " + textToUse),
      text(textToUse)
{
}

void Label::onDraw(SDL_Renderer* renderer)
{
    Helpers::fillRect(renderer, getLocalBounds(), Colors::background);
    const uint8_t fontPointSize = (float) pointSize / state->pixelScale;

    SDL_FRect rect = getLocalBoundsF();

    float marginScaled = margin / state->pixelScale;

    rect.x += marginScaled;
    rect.y += marginScaled;
    rect.w -= marginScaled * 2;
    rect.h -= marginScaled * 2;

    auto [textW, textH] = measureText(text, fontPointSize);

    // Adjust rect.y for vertical centering
    if (centerVertically)
    {
        rect.y = rect.y + (rect.h - textH) * 0.5f;
        rect.h = (float)textH; // shrink to text height
    }

    renderText(renderer, text, fontPointSize, rect, centerHorizontally);
}

