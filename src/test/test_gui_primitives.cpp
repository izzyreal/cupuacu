#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestSdlTtfGuard.hpp"
#include "gui/Component.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/DropdownMenu.hpp"
#include "gui/Label.hpp"
#include "gui/Menu.hpp"
#include "gui/MenuBar.hpp"
#include "gui/ScrollBar.hpp"
#include "gui/Window.hpp"

#include <SDL3/SDL.h>

#include <memory>
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

    std::vector<cupuacu::gui::Label *> labelChildren(cupuacu::gui::Component *parent)
    {
        std::vector<cupuacu::gui::Label *> result;
        for (const auto &child : parent->getChildren())
        {
            if (auto *label = dynamic_cast<cupuacu::gui::Label *>(child.get()))
            {
                result.push_back(label);
            }
        }
        return result;
    }

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
} // namespace

TEST_CASE("ScrollBar click and drag update value with clamping", "[gui]")
{
    cupuacu::State state{};
    double value = 20.0;

    cupuacu::gui::ScrollBar bar(
        &state, cupuacu::gui::ScrollBar::Orientation::Horizontal,
        [&]() { return value; },
        []() { return 0.0; },
        []() { return 100.0; },
        []() { return 25.0; },
        [&](const double next) { value = next; });
    bar.setVisible(true);
    bar.setBounds(0, 0, 100, 10);

    REQUIRE(bar.mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        90,
        5,
        90.0f,
        5.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));
    REQUIRE(value > 70.0);
    REQUIRE(value <= 100.0);

    REQUIRE(bar.mouseMove(cupuacu::gui::MouseEvent{
        cupuacu::gui::MOVE,
        200,
        5,
        200.0f,
        5.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));
    REQUIRE(value == 100.0);

    REQUIRE(bar.mouseUp(cupuacu::gui::MouseEvent{
        cupuacu::gui::UP,
        200,
        5,
        200.0f,
        5.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));
}

TEST_CASE("DropdownMenu expands collapses and notifies on selection change",
          "[gui]")
{
    cupuacu::State state{};
    state.menuFontSize = 32;

    cupuacu::gui::DropdownMenu dropdown(&state);
    dropdown.setVisible(true);
    dropdown.setBounds(0, 0, 160, 30);
    dropdown.setItems({"Alpha", "Beta", "Gamma"});
    dropdown.setCollapsedHeight(30);

    int callbackIndex = -1;
    dropdown.setOnSelectionChanged(
        [&](const int index) { callbackIndex = index; });

    auto labels = labelChildren(&dropdown);
    REQUIRE(labels.size() == 3);
    REQUIRE(labels[0]->isVisible());
    REQUIRE_FALSE(labels[1]->isVisible());
    REQUIRE_FALSE(labels[2]->isVisible());
    REQUIRE(dropdown.getSelectedIndex() == 0);

    REQUIRE(dropdown.mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        10,
        10,
        10.0f,
        10.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));

    REQUIRE(dropdown.getHeight() > 30);
    REQUIRE(labels[0]->isVisible());
    REQUIRE(labels[1]->isVisible());
    REQUIRE(labels[2]->isVisible());

    const int rowHeight = labels[1]->getBounds().y - labels[0]->getBounds().y;
    REQUIRE(rowHeight > 0);

    REQUIRE(dropdown.mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        10,
        rowHeight + 1,
        10.0f,
        static_cast<float>(rowHeight + 1),
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));

    REQUIRE(dropdown.getSelectedIndex() == 1);
    REQUIRE(callbackIndex == 1);
    REQUIRE(dropdown.getHeight() == 30);
    REQUIRE_FALSE(labels[0]->isVisible());
    REQUIRE(labels[1]->isVisible());
    REQUIRE_FALSE(labels[2]->isVisible());
}

TEST_CASE("Menu hover can switch open submenu between siblings", "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "menu-hover", 480, 240, SDL_WINDOW_HIDDEN);

    auto root = std::make_unique<RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 4);
    auto *fileMenu = topLevelMenus[0];
    auto *viewMenu = topLevelMenus[1];

    REQUIRE(fileMenu->mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        5,
        5,
        5.0f,
        5.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));
    REQUIRE(fileMenu->isOpen());
    REQUIRE_FALSE(viewMenu->isOpen());

    menuBar->setOpenSubMenuOnMouseOver(true);
    viewMenu->mouseEnter();

    REQUIRE_FALSE(fileMenu->isOpen());
    REQUIRE(viewMenu->isOpen());
}
