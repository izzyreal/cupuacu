#pragma once

namespace cupuacu::gui
{
    struct MenuMouseDownPlan
    {
        bool handled = true;
        bool hideMenuBarSubMenus = false;
        bool hideSelfSubMenus = false;
        bool showSelfSubMenus = false;
        bool invokeAction = false;
    };

    struct MenuMouseLeavePlan
    {
        bool markDirty = true;
        bool hideMenuBarSubMenus = false;
        bool enableOpenOnHover = false;
    };

    struct MenuMouseEnterPlan
    {
        bool markDirty = true;
        bool hideMenuBarSubMenus = false;
        bool showSelfSubMenus = false;
    };

    inline MenuMouseDownPlan planMenuMouseDown(const bool hasSubMenus,
                                               const bool isCurrentlyOpen,
                                               const bool isAvailable)
    {
        MenuMouseDownPlan plan{};

        if (!hasSubMenus)
        {
            plan.hideMenuBarSubMenus = true;
            plan.invokeAction = isAvailable;
            return plan;
        }

        if (isCurrentlyOpen)
        {
            plan.hideSelfSubMenus = true;
            plan.hideMenuBarSubMenus = true;
            return plan;
        }

        plan.hideMenuBarSubMenus = true;
        plan.showSelfSubMenus = true;
        return plan;
    }

    inline MenuMouseLeavePlan
    planMenuMouseLeave(const bool componentUnderMouseIsMenuBarOrChild,
                       const bool hasMenuBarOpenMenu)
    {
        MenuMouseLeavePlan plan{};
        if (componentUnderMouseIsMenuBarOrChild && hasMenuBarOpenMenu)
        {
            plan.hideMenuBarSubMenus = true;
            plan.enableOpenOnHover = true;
        }
        return plan;
    }

    inline MenuMouseEnterPlan
    planMenuMouseEnter(const bool hasSubMenus, const bool hasDifferentOpenMenu,
                       const bool shouldOpenOnMouseOver)
    {
        MenuMouseEnterPlan plan{};
        if (hasSubMenus && (hasDifferentOpenMenu || shouldOpenOnMouseOver))
        {
            plan.hideMenuBarSubMenus = true;
            plan.showSelfSubMenus = true;
        }
        return plan;
    }
} // namespace cupuacu::gui
