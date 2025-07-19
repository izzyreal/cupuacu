#pragma once

#include "Component.h"
#include "text.h"

class Menu : public Component {
    public:
        Menu(SDL_Rect r) { rect = r; }
        void onDraw(SDL_Renderer* renderer) override
        {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderFillRect(renderer, NULL);
            renderText(renderer, " File  View");
        }
};
