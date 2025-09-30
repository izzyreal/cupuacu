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

    // Measure text
    auto fontData = get_resource_data("Inter_18pt-Regular.ttf");
    auto fontIo = SDL_IOFromMem(fontData.data(), fontData.size());
    TTF_Font* font = TTF_OpenFontIO(fontIo, false, fontPointSize);
    if (!font)
    {
        printf("Problem opening TTF font\n");
        return;
    }

    int textW = 0, textH = 0;
    if (!TTF_GetStringSize(font, text.c_str(), text.size(), &textW, &textH))
    {
        printf("Problem sizing text: %s\n", SDL_GetError());
        TTF_CloseFont(font);
        return;
    }

    TTF_CloseFont(font);

    // Adjust rect.y for vertical centering
    if (centerVertically)
    {
        rect.y = rect.y + (rect.h - textH) * 0.5f;
        rect.h = (float)textH; // shrink to text height
    }

    renderText(renderer, text, fontPointSize, rect, centerHorizontally);
}
