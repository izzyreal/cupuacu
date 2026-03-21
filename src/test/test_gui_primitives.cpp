#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestPaths.hpp"
#include "TestSdlTtfGuard.hpp"
#include "gui/Button.hpp"
#include "gui/Component.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/DropdownMenu.hpp"
#include "gui/Label.hpp"
#include "gui/Menu.hpp"
#include "gui/MenuBar.hpp"
#include "gui/ScrollBar.hpp"
#include "gui/Slider.hpp"
#include "gui/TextInput.hpp"
#include "gui/TextButton.hpp"
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
    cupuacu::test::StateWithTestPaths state{};
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
    cupuacu::test::StateWithTestPaths state{};
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
    REQUIRE(dropdown.getHeight() >= 30);
    REQUIRE(dropdown.getHeight() >= dropdown.getRowHeight());
    REQUIRE_FALSE(labels[0]->isVisible());
    REQUIRE(labels[1]->isVisible());
    REQUIRE_FALSE(labels[2]->isVisible());
}

TEST_CASE("DropdownMenu collapsed height tracks row height after margin changes",
          "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.menuFontSize = 32;

    cupuacu::gui::DropdownMenu dropdown(&state);
    dropdown.setVisible(true);
    dropdown.setBounds(0, 0, 180, 30);
    dropdown.setItems({"Linear", "Exponential", "Logarithmic"});
    dropdown.setCollapsedHeight(30);

    dropdown.setItemMargin(10);

    REQUIRE(dropdown.getHeight() >= dropdown.getRowHeight());
}

TEST_CASE("Button momentary and toggle semantics fire callbacks only when enabled",
          "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};

    cupuacu::gui::Button momentary(&state, "Momentary",
                                   cupuacu::gui::ButtonType::Momentary);
    momentary.setVisible(true);
    momentary.setBounds(0, 0, 80, 24);

    int pressCount = 0;
    momentary.setOnPress([&]() { ++pressCount; });

    REQUIRE(momentary.mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        10,
        10,
        10.0f,
        10.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));
    REQUIRE(pressCount == 1);
    REQUIRE(momentary.mouseMove(cupuacu::gui::MouseEvent{
        cupuacu::gui::MOVE,
        120,
        10,
        120.0f,
        10.0f,
        110.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));
    REQUIRE(momentary.mouseUp(cupuacu::gui::MouseEvent{
        cupuacu::gui::UP,
        120,
        10,
        120.0f,
        10.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));

    cupuacu::gui::Button toggle(&state, "Toggle",
                                cupuacu::gui::ButtonType::Toggle);
    toggle.setVisible(true);
    toggle.setBounds(0, 0, 80, 24);

    int toggledValue = -1;
    toggle.setOnToggle([&](const bool isOn) { toggledValue = isOn ? 1 : 0; });
    REQUIRE(toggle.mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        5,
        5,
        5.0f,
        5.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));
    REQUIRE(toggle.isToggled());
    REQUIRE(toggledValue == 1);

    toggle.setEnabled(false);
    REQUIRE_FALSE(toggle.mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        5,
        5,
        5.0f,
        5.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));
    REQUIRE(toggle.isToggled());
    REQUIRE(toggledValue == 1);
}

TEST_CASE("Button can defer momentary press callbacks until mouse up", "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};

    cupuacu::gui::Button button(&state, "DeferredMomentary",
                                cupuacu::gui::ButtonType::Momentary);
    button.setVisible(true);
    button.setBounds(0, 0, 80, 24);
    button.setTriggerOnMouseUp(true);

    int pressCount = 0;
    button.setOnPress([&]() { ++pressCount; });

    REQUIRE(button.mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        10,
        10,
        10.0f,
        10.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));
    REQUIRE(pressCount == 0);

    REQUIRE(button.mouseUp(cupuacu::gui::MouseEvent{
        cupuacu::gui::UP,
        10,
        10,
        10.0f,
        10.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));
    REQUIRE(pressCount == 1);
}

TEST_CASE("TextButton updates its label text and keeps it sized to bounds", "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    cupuacu::gui::TextButton button(&state, "Play");
    button.setVisible(true);
    button.setBounds(0, 0, 120, 32);

    auto labels = labelChildren(&button);
    REQUIRE(labels.size() == 1);
    REQUIRE(labels[0]->getText() == "Play");
    const SDL_Rect originalLabelBounds = labels[0]->getBounds();
    const SDL_Rect buttonBounds = button.getLocalBounds();
    REQUIRE(originalLabelBounds.x == buttonBounds.x);
    REQUIRE(originalLabelBounds.y == buttonBounds.y);
    REQUIRE(originalLabelBounds.w == buttonBounds.w);
    REQUIRE(originalLabelBounds.h == buttonBounds.h);

    button.setText("Pause");
    labels = labelChildren(&button);
    REQUIRE(labels.size() == 1);
    REQUIRE(labels[0]->getText() == "Pause");
    const SDL_Rect updatedLabelBounds = labels[0]->getBounds();
    REQUIRE(updatedLabelBounds.x == buttonBounds.x);
    REQUIRE(updatedLabelBounds.y == buttonBounds.y);
    REQUIRE(updatedLabelBounds.w == buttonBounds.w);
    REQUIRE(updatedLabelBounds.h == buttonBounds.h);
}

TEST_CASE("TextInput handles focus, text input, and backspace directly",
          "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    cupuacu::gui::TextInput input(&state);
    input.setBounds(10, 10, 120, 28);
    input.setAllowedCharacters("0123456789.%");
    input.focusGained();

    REQUIRE(input.textInput("125%"));
    REQUIRE(input.getText() == "125%");

    SDL_KeyboardEvent backspaceEvent{};
    backspaceEvent.scancode = SDL_SCANCODE_BACKSPACE;
    REQUIRE(input.keyDown(backspaceEvent));
    REQUIRE(input.getText() == "125");

    input.focusLost();
}

TEST_CASE("Slider updates value across drag range", "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    double value = 100.0;

    cupuacu::gui::Slider slider(
        &state, [&]() { return value; }, []() { return 0.0; },
        []() { return 1000.0; }, [&](const double next) { value = next; });
    slider.setVisible(true);
    slider.setBounds(0, 0, 100, 20);

    REQUIRE(slider.mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        0,
        10,
        0.0f,
        10.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));
    REQUIRE(value == 0.0);

    REQUIRE(slider.mouseMove(cupuacu::gui::MouseEvent{
        cupuacu::gui::MOVE,
        99,
        10,
        99.0f,
        10.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));
    REQUIRE(value > 990.0);

    REQUIRE(slider.mouseUp(cupuacu::gui::MouseEvent{
        cupuacu::gui::UP,
        50,
        10,
        50.0f,
        10.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));
}

TEST_CASE("TextInput inserts at the caret after left-right navigation", "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    cupuacu::gui::TextInput input(&state);
    input.setText("abcd");
    input.focusGained();

    SDL_KeyboardEvent left{};
    left.scancode = SDL_SCANCODE_LEFT;

    REQUIRE(input.keyDown(left));
    REQUIRE(input.keyDown(left));
    REQUIRE(input.textInput("X"));
    REQUIRE(input.getText() == "abXcd");
}

TEST_CASE("TextInput replaces a shift-arrow selection on input", "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    cupuacu::gui::TextInput input(&state);
    input.setText("abcd");
    input.focusGained();

    SDL_KeyboardEvent shiftLeft{};
    shiftLeft.scancode = SDL_SCANCODE_LEFT;
    shiftLeft.mod = SDL_KMOD_SHIFT;

    REQUIRE(input.keyDown(shiftLeft));
    REQUIRE(input.keyDown(shiftLeft));
    REQUIRE(input.textInput("Z"));
    REQUIRE(input.getText() == "abZ");
}

TEST_CASE("TextInput backspace deletes the selected range", "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    cupuacu::gui::TextInput input(&state);
    input.setText("abcd");
    input.focusGained();

    SDL_KeyboardEvent shiftLeft{};
    shiftLeft.scancode = SDL_SCANCODE_LEFT;
    shiftLeft.mod = SDL_KMOD_SHIFT;
    REQUIRE(input.keyDown(shiftLeft));
    REQUIRE(input.keyDown(shiftLeft));

    SDL_KeyboardEvent backspace{};
    backspace.scancode = SDL_SCANCODE_BACKSPACE;
    REQUIRE(input.keyDown(backspace));
    REQUIRE(input.getText() == "ab");
}
