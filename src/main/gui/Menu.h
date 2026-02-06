#pragma once

#include <SDL3/SDL.h>

#include "Component.h"

#include <string>
#include <vector>
#include <functional>
#include <utility>

namespace cupuacu::gui
{

    class Label;

    class Menu : public Component
    {
    private:
        bool currentlyOpen = false;
        const std::string menuName;
        std::vector<Menu *> subMenus;
        const std::function<void()> action;
        std::function<std::string()> menuNameGetter = []
        {
            return "";
        };
        std::function<bool()> isAvailable = []
        {
            return true;
        };

        Label *label = nullptr;

        bool isFirstLevel() const;

        std::string getMenuName();

    public:
        Menu(State *, const std::string &menuNameToUse,
             const std::function<void()> &actionToUse = {});
        Menu(State *,
             const std::function<std::string()> &menuNameGetterToUse,
             const std::function<void()> &actionToUse = {});

        void setIsAvailable(const std::function<bool()> &);

        template <typename... Args> Menu *addSubMenu(Args &&...args)
        {
            auto subMenu = emplaceChild<Menu>(std::forward<Args>(args)...);
            subMenu->setVisible(false);
            subMenus.push_back(subMenu);
            return subMenus.back();
        }

        void showSubMenus();
        void hideSubMenus();

        bool isOpen()
        {
            return currentlyOpen;
        }

        void resized() override;
        void onDraw(SDL_Renderer *) override;
        bool mouseDown(const MouseEvent &) override;
        bool mouseUp(const MouseEvent &) override;
        void mouseLeave() override;
        void mouseEnter() override;
        bool shouldCaptureMouse() const override
        {
            return false;
        }
    };
} // namespace cupuacu::gui
