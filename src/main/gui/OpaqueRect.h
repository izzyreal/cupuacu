#pragma once

#include "Component.h"

class OpaqueRect : public Component {
private:
    const SDL_Color color;

public:
    OpaqueRect(CupuacuState *state, const SDL_Color colorToUse) :
        Component(state, "OpaqueRect"), color(colorToUse)
    {
    }

    void onDraw(SDL_Renderer* renderer)
    {
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        auto rect = getLocalBoundsF();
        SDL_RenderFillRect(renderer, &rect);
    }
};
