#pragma once

#include "Component.h"
#include "text.h"
#include "../CupuacuState.h"

class Menu : public Component {
    private:
        const std::string name;
        CupuacuState *state;

    public:
        Menu(const std::string nameToUse, SDL_Rect &rectToUse, CupuacuState *stateToUse) : name(nameToUse), state(stateToUse)
        {
            componentName = "Menu for " + name;
            rect = rectToUse;
        }

        void onDraw(SDL_Renderer *renderer) override
        {
            const uint8_t bg = mouseIsOver ? 40 : 0;
            SDL_SetRenderDrawColor(renderer, bg, bg, bg, 255);
            SDL_RenderFillRect(renderer, NULL);
            const uint8_t fontPointSize = state->menuFontSize / state->hardwarePixelsPerAppPixel;
            renderText(renderer, name, fontPointSize);
        }

        void mouseLeave() override
        {
            setDirty();
        }

        void mouseEnter() override
        {
            printf("mouse enter\n");
            setDirty();
        }
};

class MenuBar : public Component {

    public:
        MenuBar(SDL_Rect r, CupuacuState *s)
        {
            componentName = "MenuBar";
            rect = r;

            SDL_Rect fileMenuRect { 0, 0, 40, rect.h };
            SDL_Rect viewMenuRect { 40, 0, 100, rect.h };

            auto fileMenu = std::make_unique<Menu>("File", fileMenuRect, s);
            auto viewMenu = std::make_unique<Menu>("View", viewMenuRect, s);

            fileMenu->setDirty();
            viewMenu->setDirty();

            children.push_back(std::move(fileMenu));
            children.push_back(std::move(viewMenu));
        }
        void mouseEnter() override
        {
            printf("mouse enter menubar\n");
            setDirty();
        }
};
