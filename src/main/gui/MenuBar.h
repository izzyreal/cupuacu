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

    public:
        MenuBar(CupuacuState *state) : Component(state, "MenuBar")
        {
            auto fileMenu = std::make_unique<Menu>(state, "File");
            fileMenu->setBounds(0, 0, 40, getHeight());

            auto viewMenu = std::make_unique<Menu>(state, "View");
            viewMenu->setBounds(fileMenu->getWidth(), 0, 100, getHeight());

            fileMenu->setDirty();
            viewMenu->setDirty();

            addChildAndSetDirty(fileMenu);
            addChildAndSetDirty(viewMenu);
        }

        void mouseEnter() override
        {
            setDirty();
        }
};

