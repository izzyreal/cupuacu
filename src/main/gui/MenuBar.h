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

public:
    MenuBar(CupuacuState*);
    Menu* getOpenMenu();
    void hideSubMenus();
    void resized() override;
    void mouseEnter() override;
};
