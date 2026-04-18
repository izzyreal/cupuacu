#pragma once

#include <SDL3/SDL.h>

#include "Component.hpp"

#include <string>
#include <vector>
#include <functional>
#include <utility>

namespace cupuacu::gui
{
    struct MenuAvailability
    {
        bool available = true;
        std::string unavailableReason;
    };

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
        std::function<MenuAvailability()> availabilityGetter = []
        {
            return MenuAvailability{};
        };
        std::function<std::string()> tooltipTextGetter = []
        {
            return "";
        };

        Label *label = nullptr;

        bool isFirstLevel() const;

        bool shouldShowAsSubMenuItem() const;
        MenuAvailability getLocalAvailability() const;
        MenuAvailability getEffectiveAvailability() const;
        bool isEffectivelyAvailable() const;

    public:
        Menu(State *, const std::string &menuNameToUse,
             const std::function<void()> &actionToUse = {});
        Menu(State *, const std::function<std::string()> &menuNameGetterToUse,
             const std::function<void()> &actionToUse = {});

        void setIsAvailable(const std::function<bool()> &);
        void setAvailability(const std::function<MenuAvailability()> &);
        void setTooltipText(const std::function<std::string()> &);
        void setTooltipText(const std::string &);

        template <typename... Args> Menu *addSubMenu(Args &&...args)
        {
            auto subMenu = emplaceChild<Menu>(std::forward<Args>(args)...);
            subMenu->setVisible(false);
            subMenus.push_back(subMenu);
            return subMenus.back();
        }

        void showSubMenus();
        void hideSubMenus();

        bool isOpen() const
        {
            return currentlyOpen;
        }

        std::string getMenuName() const;
        std::string getTooltipText() const override;

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
