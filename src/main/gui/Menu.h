#pragma once

#include <SDL3/SDL.h>

#include "Component.h"

#include <string>
#include <vector>
#include <functional>
#include <utility>

struct CupuacuState;

class Menu : public Component {
private:
    bool currentlyOpen = false;
    const std::string menuName;
    std::vector<Menu*> subMenus;
    const std::function<void()> action;

public:
    Menu(CupuacuState*, const std::string menuNameToUse, const std::function<void()> actionToUse = {});

    template <typename... Args>
    void addSubMenu(Args&&... args)
    {
        auto subMenu = emplaceChildAndSetDirty<Menu>(std::forward<Args>(args)...);
        subMenu->setBounds(0, 0, 0, 0);
        subMenus.push_back(subMenu);
    }
    
    void showSubMenus();
    void hideSubMenus();
    
    void onDraw(SDL_Renderer*) override;
    bool mouseLeftButtonDown(const uint8_t numClicks, const int32_t mouseX, const int32_t mouseY) override;
    bool mouseLeftButtonUp(const uint8_t numClicks, const int32_t mouseX, const int32_t mouseY) override;
    void mouseLeave() override;
    void mouseEnter() override;
};
