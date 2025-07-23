#pragma once

#include "Component.h"

#include "../CupuacuState.h"

class SamplePoint : public Component {
    private:
        CupuacuState *state;

    public:
        SamplePoint(CupuacuState *s) { state = s; }

        void mouseEnter() override
        {
            setDirty();
        }

        void mouseLeave() override
        {
            setDirty();
        }

        void onDraw(SDL_Renderer *r) override
        {
            SDL_SetRenderDrawColor(r, 0, mouseIsOver ? 255 : 185, 0, 255);
            SDL_FRect rectToFill {0, 0, (float)rect.w, (float)rect.h};
            SDL_RenderFillRect(r, &rectToFill);
        }
};
