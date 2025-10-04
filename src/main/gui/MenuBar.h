#pragma once

#include "Component.h"

namespace cupuacu::gui {

class Menu;
class OpaqueRect;

class MenuBar : public Component {
private:
    OpaqueRect *background;
    Menu *fileMenu;
    Menu *viewMenu;
    Menu *editMenu;
    bool openSubMenuOnMouseOver;
    std::string logoData;
    SDL_Texture* logoTexture;
    int logoW = 0, logoH = 0;

public:
    void setOpenSubMenuOnMouseOver(const bool openSubMenuOnMouseOverEnabled) { openSubMenuOnMouseOver = openSubMenuOnMouseOverEnabled; }
    bool shouldOpenSubMenuOnMouseOver() const { return openSubMenuOnMouseOver; } 
    MenuBar(cupuacu::State*);
    bool hasMenuOpen();
    Menu* getOpenMenu();
    void hideSubMenus();
    void resized() override;
    void mouseEnter() override;
    void onDraw(SDL_Renderer*) override;
};
}
