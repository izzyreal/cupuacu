#pragma once

#include "Component.h"
#include "text.h"
#include "../CupuacuState.h"
#include "../actions/ShowOpenFileDialog.h"
#include "../actions/Zoom.h"

#include <SDL3/SDL.h>

class Menu : public Component {
    private:
        bool currentlyOpen = false;
        const std::string menuName;
        std::vector<Menu*> subMenus;
        const std::function<void()> action;

    public:
        Menu(CupuacuState *state, const std::string menuNameToUse, const std::function<void()> actionToUse = {}) :
            Component(state, "Menu for " + menuNameToUse), menuName(menuNameToUse), action(actionToUse)
        {
        }

        void showSubMenus()
        {
            if (subMenus.empty())
                return;

            float scale = 4.0f / state->hardwarePixelsPerAppPixel;
            int subMenuYPos = getHeight(); // already correct, don't scale

            for (auto &subMenu : subMenus)
            {
                int w = int(150 * scale);
                int h = int(20 * scale);
                subMenu->setBounds(int(5 * scale), subMenuYPos, w, h);
                subMenuYPos += h;
            }

            currentlyOpen = true;
        }
        
        void hideSubMenus()
        {
            for (auto &subMenu : subMenus)
            {
                subMenu->setBounds(0, 0, 0, 0);
            }

            currentlyOpen = false;
        }

        template <typename... Args>
        void addSubMenu(Args&&... args)
        {
            auto subMenu = emplaceChildAndSetDirty<Menu>(std::forward<Args>(args)...);
            subMenu->setBounds(0, 0, 0, 0);
            subMenus.push_back(subMenu);
        }

        void onDraw(SDL_Renderer *renderer) override
        {
            const uint8_t bg = isMouseOver() ? 80 : 40;
            SDL_SetRenderDrawColor(renderer, bg, bg, bg, 255);
            SDL_FRect r{0, 0, (float)getWidth(), (float)getHeight() };
            SDL_RenderFillRect(renderer, &r);
            const uint8_t fontPointSize = state->menuFontSize / state->hardwarePixelsPerAppPixel;
            renderText(renderer, menuName, fontPointSize);
        }

        bool mouseLeftButtonDown(const uint8_t numClicks, const int32_t mouseX, const int32_t mouseY) override
        {
            const bool wasCurrentlyOpen = currentlyOpen;

            if (subMenus.empty())
            {
                if (action)
                {
                    action();
                }
                state->hideSubMenus();
                return true;
            }

            if (wasCurrentlyOpen)
            {
                state->hideSubMenus();
                return true;
            }

            state->hideSubMenus();
            showSubMenus();

            return true;
        }

        bool mouseLeftButtonUp(const uint8_t numClicks, const int32_t mouseX, const int32_t mouseY) override
        {
            return true;
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
        Menu* fileMenu = nullptr;
        Menu* viewMenu = nullptr;

    public:
        MenuBar(CupuacuState *stateToUse) : Component(stateToUse, "MenuBar")
        {
            fileMenu = emplaceChildAndSetDirty<Menu>(state, "File");
            viewMenu = emplaceChildAndSetDirty<Menu>(state, "View");

            fileMenu->addSubMenu(state, "Load", [&]{
                        showOpenFileDialog(state);
                    });
            fileMenu->addSubMenu(state, "Save", [&]{
                        printf("Saving a file\n");
                    });

            viewMenu->addSubMenu(state, "Reset zoom (Esc)", [&]{
                        resetZoom(state);
                    });
            viewMenu->addSubMenu(state, "Zoom out horiz. (Q)", [&]{
                        tryZoomOutHorizontally(state);
                    });
            viewMenu->addSubMenu(state, "Zoom in horiz. (W)", [&]{
                        tryZoomInHorizontally(state);
                    });
            viewMenu->addSubMenu(state, "Zoom out vert. (E)", [&]{
                        tryZoomOutVertically(state, 1);
                    });
            viewMenu->addSubMenu(state, "Zoom in vert. (R)", [&]{
                        zoomInVertically(state, 1);
                    });
        }

        void hideSubMenus()
        {
            fileMenu->hideSubMenus();
            viewMenu->hideSubMenus();
        }
void resized() override
{
    float scale = 4.0f / state->hardwarePixelsPerAppPixel;

    int fileW = int(40 * scale);
    int viewW = int(100 * scale);
    int h = getHeight(); // keep unscaled!

    fileMenu->setBounds(0, 0, fileW, h);
    viewMenu->setBounds(fileW, 0, viewW, h);
}
        void mouseEnter() override
        {
            setDirty();
        }
};

