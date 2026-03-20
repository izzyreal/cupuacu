#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestSdlTtfGuard.hpp"
#include "gui/Component.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/Menu.hpp"
#include "gui/MenuBar.hpp"
#include "gui/MenuPlanning.hpp"

#include <vector>

namespace
{
    class RootComponent : public cupuacu::gui::Component
    {
    public:
        explicit RootComponent(cupuacu::State *state)
            : Component(state, "Root")
        {
        }
    };

    std::vector<cupuacu::gui::Menu *> menuChildren(cupuacu::gui::Component *parent)
    {
        std::vector<cupuacu::gui::Menu *> result;
        for (const auto &child : parent->getChildren())
        {
            if (auto *menu = dynamic_cast<cupuacu::gui::Menu *>(child.get()))
            {
                result.push_back(menu);
            }
        }
        return result;
    }

    cupuacu::gui::MouseEvent leftMouseDown()
    {
        return cupuacu::gui::MouseEvent{
            cupuacu::gui::DOWN, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f,
            cupuacu::gui::MouseButtonState{true, false, false}, 1};
    }
} // namespace

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

TEST_CASE("Menu runtime leaf actions respect availability", "[gui]")
{
    cupuacu::State state{};

    int availableCalls = 0;
    cupuacu::gui::Menu available(&state, "Available",
                                 [&]() { ++availableCalls; });
    REQUIRE(available.mouseDown(leftMouseDown()));
    REQUIRE(availableCalls == 1);

    int unavailableCalls = 0;
    cupuacu::gui::Menu unavailable(&state, "Unavailable",
                                   [&]() { ++unavailableCalls; });
    unavailable.setIsAvailable([]() { return false; });
    REQUIRE(unavailable.mouseDown(leftMouseDown()));
    REQUIRE(unavailableCalls == 0);
}

TEST_CASE("Menu runtime shows and hides stacked submenus with dynamic names",
          "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    RootComponent root(&state);
    auto *parent = root.emplaceChild<cupuacu::gui::Menu>(&state, "Parent");
    parent->setBounds(0, 0, 80, 24);

    auto *first = parent->addSubMenu(&state, []() { return std::string{"One"}; });
    auto *second =
        parent->addSubMenu(&state, []() { return std::string{"Second option"}; });

    REQUIRE_FALSE(parent->isOpen());
    REQUIRE_FALSE(first->isVisible());
    REQUIRE_FALSE(second->isVisible());

    parent->showSubMenus();

    REQUIRE(parent->isOpen());
    REQUIRE(first->isVisible());
    REQUIRE(second->isVisible());
    REQUIRE(first->getYPos() == parent->getHeight());
    REQUIRE(second->getYPos() == first->getYPos() + first->getHeight());
    REQUIRE(first->getWidth() > 1);
    REQUIRE(second->getWidth() == first->getWidth());
    REQUIRE(first->getHeight() > 0);

    parent->hideSubMenus();

    REQUIRE_FALSE(parent->isOpen());
    REQUIRE_FALSE(first->isVisible());
    REQUIRE_FALSE(second->isVisible());
}

TEST_CASE("Menu runtime top-level click toggles submenu visibility", "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    RootComponent root(&state);
    auto *menuBar = root.emplaceChild<cupuacu::gui::MenuBar>(&state);
    menuBar->setBounds(0, 0, 480, 40);

    auto topLevelMenus = menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 6);
    auto *fileMenu = topLevelMenus[0];
    auto subMenus = menuChildren(fileMenu);
    REQUIRE_FALSE(subMenus.empty());

    REQUIRE(fileMenu->mouseDown(leftMouseDown()));
    REQUIRE(fileMenu->isOpen());
    for (auto *submenu : subMenus)
    {
        REQUIRE(submenu->isVisible());
    }

    REQUIRE(fileMenu->mouseDown(leftMouseDown()));
    REQUIRE_FALSE(fileMenu->isOpen());
    for (auto *submenu : subMenus)
    {
        REQUIRE_FALSE(submenu->isVisible());
    }
}
