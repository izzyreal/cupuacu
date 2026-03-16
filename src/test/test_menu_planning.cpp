#include <catch2/catch_test_macros.hpp>

#include "gui/MenuPlanning.hpp"

TEST_CASE("Menu planning opens and closes submenu parents on click", "[gui]")
{
    SECTION("closed parent menu opens and closes siblings")
    {
        const auto plan = cupuacu::gui::planMenuMouseDown(true, false, true);

        REQUIRE(plan.handled);
        REQUIRE(plan.hideMenuBarSubMenus);
        REQUIRE_FALSE(plan.hideSelfSubMenus);
        REQUIRE(plan.showSelfSubMenus);
        REQUIRE_FALSE(plan.invokeAction);
    }

    SECTION("open parent menu closes all menus")
    {
        const auto plan = cupuacu::gui::planMenuMouseDown(true, true, true);

        REQUIRE(plan.handled);
        REQUIRE(plan.hideMenuBarSubMenus);
        REQUIRE(plan.hideSelfSubMenus);
        REQUIRE_FALSE(plan.showSelfSubMenus);
        REQUIRE_FALSE(plan.invokeAction);
    }
}

TEST_CASE("Menu planning invokes leaf actions only when available", "[gui]")
{
    const auto availablePlan =
        cupuacu::gui::planMenuMouseDown(false, false, true);
    const auto unavailablePlan =
        cupuacu::gui::planMenuMouseDown(false, false, false);

    REQUIRE(availablePlan.hideMenuBarSubMenus);
    REQUIRE(availablePlan.invokeAction);

    REQUIRE(unavailablePlan.hideMenuBarSubMenus);
    REQUIRE_FALSE(unavailablePlan.invokeAction);
}

TEST_CASE("Menu leave planning closes menus and arms sibling hover switching", "[gui]")
{
    const auto activePlan = cupuacu::gui::planMenuMouseLeave(true, true);
    const auto inactivePlan = cupuacu::gui::planMenuMouseLeave(false, true);

    REQUIRE(activePlan.markDirty);
    REQUIRE(activePlan.hideMenuBarSubMenus);
    REQUIRE(activePlan.enableOpenOnHover);

    REQUIRE(inactivePlan.markDirty);
    REQUIRE_FALSE(inactivePlan.hideMenuBarSubMenus);
    REQUIRE_FALSE(inactivePlan.enableOpenOnHover);
}

TEST_CASE("Menu enter planning switches to sibling menus when appropriate", "[gui]")
{
    const auto differentOpenMenuPlan =
        cupuacu::gui::planMenuMouseEnter(true, true, false);
    const auto hoverOpenPlan =
        cupuacu::gui::planMenuMouseEnter(true, false, true);
    const auto leafPlan =
        cupuacu::gui::planMenuMouseEnter(false, true, true);

    REQUIRE(differentOpenMenuPlan.markDirty);
    REQUIRE(differentOpenMenuPlan.hideMenuBarSubMenus);
    REQUIRE(differentOpenMenuPlan.showSelfSubMenus);

    REQUIRE(hoverOpenPlan.hideMenuBarSubMenus);
    REQUIRE(hoverOpenPlan.showSelfSubMenus);

    REQUIRE(leafPlan.markDirty);
    REQUIRE_FALSE(leafPlan.hideMenuBarSubMenus);
    REQUIRE_FALSE(leafPlan.showSelfSubMenus);
}
