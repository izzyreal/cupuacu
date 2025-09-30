#pragma once

#include "Component.h"

class OpaqueRect : public Component {
    public:
        OpaqueRect(CupuacuState *state) : Component(state, "OpaqueRect") {}
        void onDraw(SDL_Renderer* renderer)
        {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            auto rect = getLocalBoundsF();
            SDL_RenderFillRect(renderer, &rect);
        }
};
