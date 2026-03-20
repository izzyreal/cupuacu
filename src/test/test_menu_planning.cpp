#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestSdlTtfGuard.hpp"
#include "gui/Component.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/Menu.hpp"
#include "gui/MenuBar.hpp"
#include "gui/MenuPlanning.hpp"
#include "gui/Window.hpp"

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
    REQUIRE(first->getYPos() == 0);
    REQUIRE(second->getYPos() == first->getYPos() + first->getHeight());
    REQUIRE(first->getWidth() > 1);
    REQUIRE(second->getWidth() == first->getWidth());
    REQUIRE(first->getHeight() > 0);

    parent->hideSubMenus();

    REQUIRE_FALSE(parent->isOpen());
    REQUIRE_FALSE(first->isVisible());
    REQUIRE_FALSE(second->isVisible());
}

TEST_CASE("Menu runtime positions nested submenus to the side with slight overlap",
          "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    RootComponent root(&state);
    auto *parent = root.emplaceChild<cupuacu::gui::Menu>(&state, "Parent");
    parent->setBounds(0, 0, 80, 24);
    auto *child = parent->addSubMenu(&state, "Child");
    auto *grandChild = child->addSubMenu(&state, "Grandchild");

    parent->showSubMenus();
    child->showSubMenus();

    REQUIRE(grandChild->isVisible());
    REQUIRE(grandChild->getYPos() == 0);
    REQUIRE(grandChild->getXPos() == child->getWidth() - 10);
}

TEST_CASE("Menu runtime hides dynamic submenu items with empty names", "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    RootComponent root(&state);
    auto *parent = root.emplaceChild<cupuacu::gui::Menu>(&state, "Parent");
    parent->setBounds(0, 0, 80, 24);

    auto *visible = parent->addSubMenu(&state, []() { return std::string{"One"}; });
    auto *hidden = parent->addSubMenu(&state, []() { return std::string{}; });

    parent->showSubMenus();

    REQUIRE(visible->isVisible());
    REQUIRE_FALSE(hidden->isVisible());
}

TEST_CASE("Menu runtime propagates parent unavailability to submenu actions",
          "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    RootComponent root(&state);
    auto *parent = root.emplaceChild<cupuacu::gui::Menu>(&state, "Parent");
    parent->setBounds(0, 0, 80, 24);

    int childCalls = 0;
    auto *child = parent->addSubMenu(&state, "Child", [&]() { ++childCalls; });
    parent->setIsAvailable([]() { return false; });

    parent->showSubMenus();
    REQUIRE(child->isVisible());

    REQUIRE(child->mouseDown(leftMouseDown()));
    REQUIRE(childCalls == 0);
}

TEST_CASE("Menu runtime propagates ancestor unavailability recursively",
          "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    RootComponent root(&state);
    auto *parent = root.emplaceChild<cupuacu::gui::Menu>(&state, "Parent");
    parent->setBounds(0, 0, 80, 24);

    int grandChildCalls = 0;
    auto *child = parent->addSubMenu(&state, "Child");
    auto *grandChild =
        child->addSubMenu(&state, "Grandchild", [&]() { ++grandChildCalls; });
    parent->setIsAvailable([]() { return false; });

    parent->showSubMenus();
    child->showSubMenus();
    REQUIRE(grandChild->isVisible());

    REQUIRE(grandChild->mouseDown(leftMouseDown()));
    REQUIRE(grandChildCalls == 0);
}

TEST_CASE("Menu runtime hovering a sibling item closes the previous nested submenu",
          "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "menu-hover", 320, 180, SDL_WINDOW_HIDDEN);
    auto root = std::make_unique<RootComponent>(&state);
    auto *parent = root->emplaceChild<cupuacu::gui::Menu>(&state, "Parent");
    parent->setBounds(0, 0, 80, 24);

    auto *first = parent->addSubMenu(&state, "First");
    auto *second = parent->addSubMenu(&state, "Second");
    auto *firstChild = first->addSubMenu(&state, "First child");

    auto menuBar = std::make_unique<cupuacu::gui::MenuBar>(&state);
    menuBar->setBounds(0, 0, 200, 40);
    auto *menuBarPtr = root->addChild(menuBar);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBarPtr);

    parent->showSubMenus();
    first->showSubMenus();
    REQUIRE(first->isOpen());
    REQUIRE(firstChild->isVisible());

    second->mouseEnter();

    REQUIRE_FALSE(first->isOpen());
    REQUIRE_FALSE(firstChild->isVisible());
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
