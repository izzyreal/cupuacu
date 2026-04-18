#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestPaths.hpp"
#include "gui/Component.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/MenuLayoutPlanning.hpp"
#include "gui/Menu.hpp"
#include "gui/MenuBar.hpp"
#include "gui/MenuPlanning.hpp"

#include <vector>

namespace
{
    class RootComponent : public cupuacu::gui::Component
    {
    public:
        explicit RootComponent(cupuacu::State *state) : Component(state, "Root")
        {
        }
    };

    std::vector<cupuacu::gui::Menu *>
    menuChildren(cupuacu::gui::Component *parent)
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
            cupuacu::gui::DOWN,
            0,
            0,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            cupuacu::gui::MouseButtonState{true, false, false},
            1};
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

TEST_CASE("Menu leave planning closes menus and arms sibling hover switching",
          "[gui]")
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

TEST_CASE("Menu enter planning switches to sibling menus when appropriate",
          "[gui]")
{
    const auto differentOpenMenuPlan =
        cupuacu::gui::planMenuMouseEnter(true, true, false);
    const auto hoverOpenPlan =
        cupuacu::gui::planMenuMouseEnter(true, false, true);
    const auto leafPlan = cupuacu::gui::planMenuMouseEnter(false, true, true);

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
    cupuacu::test::StateWithTestPaths state{};

    int availableCalls = 0;
    cupuacu::gui::Menu available(&state, "Available",
                                 [&]()
                                 {
                                     ++availableCalls;
                                 });
    REQUIRE(available.mouseDown(leftMouseDown()));
    REQUIRE(availableCalls == 1);

    int unavailableCalls = 0;
    cupuacu::gui::Menu unavailable(&state, "Unavailable",
                                   [&]()
                                   {
                                       ++unavailableCalls;
                                   });
    unavailable.setIsAvailable(
        []()
        {
            return false;
        });
    REQUIRE(unavailable.mouseDown(leftMouseDown()));
    REQUIRE(unavailableCalls == 0);
}

TEST_CASE("Menu tooltips combine action text with unavailability reason",
          "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};

    cupuacu::gui::Menu menu(&state, "Unavailable");
    menu.setTooltipText("Performs the action.");
    menu.setAvailability(
        []()
        {
            return cupuacu::gui::MenuAvailability{
                .available = false,
                .unavailableReason = "A prerequisite is missing"};
        });

    REQUIRE(menu.getTooltipText() ==
            "Performs the action.\n\nCurrently unavailable: A prerequisite is "
            "missing");
}

TEST_CASE("Menu tooltips inherit unavailable reason from ancestors", "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    RootComponent root(&state);

    auto *parent = root.emplaceChild<cupuacu::gui::Menu>(&state, "Parent");
    auto *child = parent->addSubMenu(&state, "Child");
    child->setTooltipText("Child action.");
    parent->setAvailability(
        []()
        {
            return cupuacu::gui::MenuAvailability{
                .available = false,
                .unavailableReason = "Parent condition is not satisfied"};
        });

    REQUIRE(child->getTooltipText() ==
            "Child action.\n\nCurrently unavailable: Parent condition is not "
            "satisfied");
}

TEST_CASE(
    "Menu submenu layout planning stacks first-level submenus with dynamic "
    "names",
    "[gui]")
{
    const auto plan = cupuacu::gui::planMenuSubMenuLayout(
        true, 80, 24, 32, 10, 64, {20, 90}, {true, true});

    REQUIRE(plan.size() == 2);
    REQUIRE(plan[0].visible);
    REQUIRE(plan[1].visible);
    REQUIRE(plan[0].x == 0);
    REQUIRE(plan[0].y == 24);
    REQUIRE(plan[1].y == 56);
    REQUIRE(plan[0].width == plan[1].width);
    REQUIRE(plan[0].width == 154);
    REQUIRE(plan[0].height == 32);
}

TEST_CASE(
    "Menu submenu layout planning positions nested submenus to the side with "
    "slight overlap",
    "[gui]")
{
    const auto plan = cupuacu::gui::planMenuSubMenuLayout(false, 144, 24, 32,
                                                          10, 64, {90}, {true});

    REQUIRE(plan.size() == 1);
    REQUIRE(plan[0].visible);
    REQUIRE(plan[0].y == 0);
    REQUIRE(plan[0].x == 134);
}

TEST_CASE(
    "Menu submenu layout planning hides dynamic submenu items with empty names",
    "[gui]")
{
    const auto plan = cupuacu::gui::planMenuSubMenuLayout(
        true, 80, 24, 32, 10, 64, {20, 0}, {true, false});

    REQUIRE(plan.size() == 2);
    REQUIRE(plan[0].visible);
    REQUIRE_FALSE(plan[1].visible);
}

TEST_CASE("Menu runtime propagates parent unavailability to submenu actions",
          "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    RootComponent root(&state);
    auto *parent = root.emplaceChild<cupuacu::gui::Menu>(&state, "Parent");
    parent->setBounds(0, 0, 80, 24);

    int childCalls = 0;
    auto *child = parent->addSubMenu(&state, "Child",
                                     [&]()
                                     {
                                         ++childCalls;
                                     });
    parent->setIsAvailable(
        []()
        {
            return false;
        });

    REQUIRE(child->mouseDown(leftMouseDown()));
    REQUIRE(childCalls == 0);
}

TEST_CASE("Menu runtime propagates ancestor unavailability recursively",
          "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    RootComponent root(&state);
    auto *parent = root.emplaceChild<cupuacu::gui::Menu>(&state, "Parent");
    parent->setBounds(0, 0, 80, 24);

    int grandChildCalls = 0;
    auto *child = parent->addSubMenu(&state, "Child");
    auto *grandChild = child->addSubMenu(&state, "Grandchild",
                                         [&]()
                                         {
                                             ++grandChildCalls;
                                         });
    parent->setIsAvailable(
        []()
        {
            return false;
        });

    REQUIRE(grandChild->mouseDown(leftMouseDown()));
    REQUIRE(grandChildCalls == 0);
}

TEST_CASE(
    "Menu runtime brings an opened nested submenu row in front of later "
    "siblings",
    "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    RootComponent root(&state);

    auto *fileMenu = root.emplaceChild<cupuacu::gui::Menu>(&state, "File");
    fileMenu->setBounds(0, 0, 160, 40);

    auto *first = fileMenu->addSubMenu(&state, "First");
    auto *recent = fileMenu->addSubMenu(&state, "Recent");
    auto *last = fileMenu->addSubMenu(&state, "Last");
    (void)first;
    (void)last;

    recent->addSubMenu(&state, "Entry 1");
    recent->addSubMenu(&state, "Entry 2");

    recent->showSubMenus();

    REQUIRE(fileMenu->getChildren().back().get() == recent);
}
