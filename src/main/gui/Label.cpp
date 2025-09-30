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
    const uint8_t fontPointSize = state->menuFontSize / state->pixelScale;

    SDL_FRect rect = getBounds();

    rect.x += margin;
    rect.y += margin;
    rect.w -= margin * 2;
    rect.h -= margin * 2;

    auto [textW, textH] = measureText(text, fontPointSize);

    // Adjust rect.y for vertical centering
    if (centerVertically)
    {
        rect.y = rect.y + (rect.h - textH) * 0.5f;
        rect.h = (float)textH; // shrink to text height
    }

    renderText(renderer, text, fontPointSize, rect, centerHorizontally);
}

