#pragma once

#include "Component.h"

class OpaqueRect : public Component {
    public:
        OpaqueRect(CupuacuState *state) : Component(state, "OpaqueRect") {}

        void onDraw(SDL_Renderer *renderer) override
        {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_FRect rectToFill {0.f, 0.f, (float)getWidth(), (float)getHeight()};
            SDL_RenderFillRect(renderer, &rectToFill);
        }
};
