#pragma once

#include "Component.h"
#include "text.h"
#include "../CupuacuState.h"

class Menu : public Component {
    private:
        const std::string menuName;

    public:
        Menu(CupuacuState *state, const std::string menuNameToUse) :
            Component(state, "Menu for " + menuNameToUse), menuName(menuNameToUse)
        {
        }

        void onDraw(SDL_Renderer *renderer) override
        {
            const uint8_t bg = isMouseOver() ? 40 : 0;
            SDL_SetRenderDrawColor(renderer, bg, bg, bg, 255);
            SDL_RenderFillRect(renderer, NULL);
            const uint8_t fontPointSize = state->menuFontSize / state->hardwarePixelsPerAppPixel;
            renderText(renderer, menuName, fontPointSize);
        }

        void mouseLeave() override
        {
            setDirty();
        }

        void mouseEnter() override
        {
            setDirty();
        }
};

class MenuBar : public Component {

    private:
        Component* fileMenu = nullptr;
        Component* viewMenu = nullptr;

    public:
        MenuBar(CupuacuState *state) : Component(state, "MenuBar")
        {
            fileMenu = emplaceChildAndSetDirty<Menu>(state, "File");
            viewMenu = emplaceChildAndSetDirty<Menu>(state, "View");
        }

        void resized() override
        {
            fileMenu->setBounds(0, 0, 40, getHeight());
            viewMenu->setBounds(fileMenu->getWidth(), 0, 100, getHeight());
        }

        void mouseEnter() override
        {
            setDirty();
        }
};

