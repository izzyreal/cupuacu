#pragma once

#include "Component.h"

namespace cupuacu::gui
{

    class Menu;
    class OpaqueRect;

    class MenuBar : public Component
    {
    private:
        OpaqueRect *background;
        Menu *fileMenu;
        Menu *viewMenu;
        Menu *editMenu;
        Menu *optionsMenu;
        bool openSubMenuOnMouseOver = false;
        std::string logoData;
        SDL_Texture *logoTexture;
        int logoW = 0, logoH = 0;

    public:
        void
        setOpenSubMenuOnMouseOver(const bool openSubMenuOnMouseOverEnabled);
        bool shouldOpenSubMenuOnMouseOver() const;
        MenuBar(State *);
        bool hasMenuOpen();
        Menu *getOpenMenu() const;
        void hideSubMenus();
        void resized() override;
        bool mouseDown(const MouseEvent &) override;
        void mouseEnter() override;
        void onDraw(SDL_Renderer *) override;
    };
} // namespace cupuacu::gui
