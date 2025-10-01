#pragma once

#include "Component.h"

class Menu;
class OpaqueRect;
struct CupuacuState;

class MenuBar : public Component {
private:
    OpaqueRect *background = nullptr;
    Menu *fileMenu = nullptr;
    Menu *viewMenu = nullptr;
    bool openSubMenuOnMouseOver = false;
    std::string logoData;
    SDL_Texture* logoTexture = nullptr;
    int logoW = 0, logoH = 0;

public:
    void setOpenSubMenuOnMouseOver(const bool openSubMenuOnMouseOverEnabled) { openSubMenuOnMouseOver = openSubMenuOnMouseOverEnabled; }
    bool shouldOpenSubMenuOnMouseOver() const { return openSubMenuOnMouseOver; } 
    MenuBar(CupuacuState*);
    Menu* getOpenMenu();
    void hideSubMenus();
    void resized() override;
    void mouseEnter() override;
    void onDraw(SDL_Renderer*) override;
};
