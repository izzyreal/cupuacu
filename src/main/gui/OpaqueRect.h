#pragma once

#include "Component.h"

class OpaqueRect : public Component {
    public:
        OpaqueRect(const SDL_Rect &rectToUse) { rect = rectToUse; }

        void onDraw(SDL_Renderer *renderer) override
        {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_FRect rectToFill {0.f, 0.f, (float)rect.w, (float)rect.h};
            SDL_RenderFillRect(renderer, &rectToFill);
        }
};
