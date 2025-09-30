#pragma once

#include <SDL3/SDL.h>

#include "Component.h"
#include "SubMenuPanel.h"

#include <string>
#include <vector>
#include <functional>
#include <utility>

struct CupuacuState;
class Label;

class Menu : public Component {
private:
    bool depthIs0 = false;
    bool currentlyOpen = false;
    const std::string menuName;
    std::vector<Menu*> subMenus;
    const std::function<void()> action;

    Label *label = nullptr;
    SubMenuPanel *subMenuPanel;

public:
    Menu(CupuacuState*, const std::string menuNameToUse, const std::function<void()> actionToUse = {});

    void enableDepthIs0();

    template <typename... Args>
    void addSubMenu(Args&&... args)
    {
        auto subMenu = emplaceChildAndSetDirty<Menu>(std::forward<Args>(args)...);
        subMenu->setBounds(0, 0, 0, 0);
        subMenus.push_back(subMenu);
    }
    
    void showSubMenus();
    void hideSubMenus();

    void resized() override;
    void onDraw(SDL_Renderer*) override;
    bool mouseDown(const MouseEvent&) override;
    bool mouseUp(const MouseEvent&) override;
    void mouseLeave() override;
    void mouseEnter() override;
};

