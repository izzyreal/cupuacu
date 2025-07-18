#pragma once

#include "Component.h"
#include "text.h"

class Menu : public Component {
    public:
        void onDraw(SDL_Renderer* renderer) override
        {
            printf("drawing menu\n");
            renderText(renderer, "Foo");
        }
};
