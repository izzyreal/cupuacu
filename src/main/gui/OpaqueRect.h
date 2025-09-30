#pragma once

#include "Component.h"

class OpaqueRect : public Component {
    public:
        OpaqueRect(CupuacuState *state) : Component(state, "OpaqueRect") {}
        void onDraw(SDL_Renderer* renderer)
        {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);

            auto absBounds = getAbsoluteBounds();
            for (const auto& dirty : state->dirtyRects)
            {
                SDL_FRect intersection;
                if (SDL_GetRectIntersectionFloat(&dirty, &absBounds, &intersection))
                {
                    intersection.x -= absBounds.x;
                    intersection.y -= absBounds.y;
                    SDL_RenderFillRect(renderer, &intersection);
                }
            }
        }
};
