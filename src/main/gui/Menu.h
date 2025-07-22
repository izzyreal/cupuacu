#pragma once

#include "Component.h"
#include "text.h"
#include "../CupuacuState.h"

class Menu : public Component {
    private:
        CupuacuState *state;
    public:
        Menu(SDL_Rect r, CupuacuState *s) { rect = r; state = s;}
        void onDraw(SDL_Renderer *renderer) override
        {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderFillRect(renderer, NULL);
            const uint8_t fontPointSize = state->menuFontSize / state->hardwarePixelsPerAppPixel;
            renderText(renderer, " File  View", fontPointSize);
        }
};
