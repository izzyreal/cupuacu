#pragma once

#include "Component.h"

class Menu;
struct CupuacuState;

class MenuBar : public Component {
private:
    Menu* fileMenu = nullptr;
    Menu* viewMenu = nullptr;

public:
    MenuBar(CupuacuState*);
    Menu* getOpenMenu();
    void hideSubMenus();
    void resized() override;
    void mouseEnter() override;
};
