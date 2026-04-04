#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestPaths.hpp"
#include "TestSdlTtfGuard.hpp"
#include "gui/Button.hpp"
#include "gui/Component.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/DropdownMenu.hpp"
#include "gui/Label.hpp"
#include "gui/LabeledField.hpp"
#include "gui/Menu.hpp"
#include "gui/MenuBar.hpp"
#include "gui/ScrollBar.hpp"
#include "gui/Slider.hpp"
#include "gui/StatusBar.hpp"
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

    template <typename T>
    T *findFirstRecursive(cupuacu::gui::Component *root)
    {
        if (root == nullptr)
        {
            return nullptr;
        }

        if (auto *typed = dynamic_cast<T *>(root))
        {
            return typed;
        }

        for (const auto &child : root->getChildren())
        {
            if (auto *found = findFirstRecursive<T>(child.get()))
            {
                return found;
            }
        }

        return nullptr;
    }

    cupuacu::gui::LabeledField *findStatusField(cupuacu::gui::StatusBar *statusBar,
                                                const std::string_view label)
    {
        if (!statusBar)
        {
            return nullptr;
        }

        for (const auto &child : statusBar->getChildren())
        {
            if (child->getComponentName() ==
                std::string("LabeledField for ") + std::string(label))
            {
                return dynamic_cast<cupuacu::gui::LabeledField *>(child.get());
            }
        }

        return nullptr;
    }

    struct StatusBarHarness
    {
        std::unique_ptr<cupuacu::gui::Window> window;
        cupuacu::gui::StatusBar *statusBar = nullptr;
    };

    StatusBarHarness createStatusBarHarness(cupuacu::State *state,
                                            const int64_t frameCount)
    {
        cupuacu::test::ensureSdlTtfInitialized();
        cupuacu::test::ensureTestPaths(*state, "status-bar");

        auto &session = state->getActiveDocumentSession();
        session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2,
                                    frameCount);
        session.syncSelectionAndCursorToDocumentLength();

        StatusBarHarness harness{};
        harness.window = std::make_unique<cupuacu::gui::Window>(
            state, "status-bar-test", 700, 40, SDL_WINDOW_HIDDEN);
        auto root = std::make_unique<RootComponent>(state);
        root->setBounds(0, 0, 700, 40);
        harness.statusBar = root->emplaceChild<cupuacu::gui::StatusBar>(state);
        harness.statusBar->setBounds(0, 0, 700, 24);
        harness.window->setRootComponent(std::move(root));
        return harness;
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

TEST_CASE("DropdownMenu toggles popup state while keeping collapsed labels in place",
          "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.menuFontSize = 32;

    cupuacu::gui::DropdownMenu dropdown(&state);
    dropdown.setVisible(true);
    dropdown.setBounds(0, 0, 160, 30);
    dropdown.setItems({"Alpha", "Beta", "Gamma"});
    dropdown.setCollapsedHeight(30);

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

    REQUIRE(dropdown.isExpanded());
    REQUIRE(dropdown.getHeight() == 30);
    REQUIRE(labels[0]->isVisible());
    REQUIRE_FALSE(labels[1]->isVisible());
    REQUIRE_FALSE(labels[2]->isVisible());

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

    REQUIRE_FALSE(dropdown.isExpanded());
    REQUIRE(dropdown.getSelectedIndex() == 0);
}

TEST_CASE("DropdownMenu expansion keeps the collapsed control bounds unchanged",
          "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.menuFontSize = 32;

    cupuacu::gui::DropdownMenu dropdown(&state);
    dropdown.setVisible(true);
    dropdown.setBounds(8, 90, 160, 30);
    dropdown.setItems({"Alpha", "Beta", "Gamma", "Delta"});
    dropdown.setCollapsedHeight(30);
    dropdown.setSelectedIndex(2);

    const int collapsedY = dropdown.getYPos();
    const int collapsedHeight = dropdown.getHeight();

    dropdown.setExpanded(true);

    REQUIRE(dropdown.isExpanded());
    REQUIRE(dropdown.getYPos() == collapsedY);
    REQUIRE(dropdown.getHeight() == collapsedHeight);

    dropdown.setExpanded(false);

    REQUIRE_FALSE(dropdown.isExpanded());
    REQUIRE(dropdown.getYPos() == collapsedY);
    REQUIRE(dropdown.getHeight() == collapsedHeight);
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

TEST_CASE("TextInput escape cancels without submitting", "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    cupuacu::gui::TextInput input(&state);
    input.setText("42");

    int finishedCount = 0;
    int canceledCount = 0;
    input.setOnEditingFinished(
        [&](const std::string &)
        {
            ++finishedCount;
        });
    input.setOnEditingCanceled(
        [&]()
        {
            ++canceledCount;
        });

    input.focusGained();

    SDL_KeyboardEvent escape{};
    escape.scancode = SDL_SCANCODE_ESCAPE;
    REQUIRE(input.keyDown(escape));
    REQUIRE(finishedCount == 0);
    REQUIRE(canceledCount == 1);
}

TEST_CASE("StatusBar inline position edit submits on enter and cancels on escape",
          "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    auto harness = createStatusBarHarness(&state, 1024);

    auto *posField = findStatusField(harness.statusBar, "Pos");
    REQUIRE(posField != nullptr);

    harness.statusBar->timerCallback();
    REQUIRE(posField->getValue() == "0");

    REQUIRE(posField->mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        10,
        10,
        10.0f,
        10.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        2}));
    REQUIRE(posField->isEditing());

    auto *editor = findFirstRecursive<cupuacu::gui::TextInput>(posField);
    REQUIRE(editor != nullptr);
    REQUIRE(editor->textInput("12"));

    state.getActiveDocumentSession().cursor = 77;
    harness.statusBar->timerCallback();
    REQUIRE(editor->getText() == "12");

    SDL_KeyboardEvent enter{};
    enter.scancode = SDL_SCANCODE_RETURN;
    REQUIRE(editor->keyDown(enter));
    harness.statusBar->timerCallback();
    REQUIRE_FALSE(posField->isEditing());
    REQUIRE(state.getActiveDocumentSession().cursor == 12);
    REQUIRE(posField->getValue() == "12");

    REQUIRE(posField->mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        10,
        10,
        10.0f,
        10.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        2}));
    REQUIRE(posField->isEditing());
    REQUIRE(editor->textInput("34"));

    SDL_KeyboardEvent escape{};
    escape.scancode = SDL_SCANCODE_ESCAPE;
    REQUIRE(editor->keyDown(escape));
    harness.statusBar->timerCallback();
    REQUIRE_FALSE(posField->isEditing());
    REQUIRE(state.getActiveDocumentSession().cursor == 12);
    REQUIRE(posField->getValue() == "12");
}

TEST_CASE("StatusBar inline length edit creates a selection from the cursor",
          "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    auto harness = createStatusBarHarness(&state, 1024);

    auto *lengthField = findStatusField(harness.statusBar, "Len");
    auto *startField = findStatusField(harness.statusBar, "St");
    auto *endField = findStatusField(harness.statusBar, "End");
    REQUIRE(lengthField != nullptr);
    REQUIRE(startField != nullptr);
    REQUIRE(endField != nullptr);

    state.getActiveDocumentSession().cursor = 100;
    harness.statusBar->timerCallback();

    REQUIRE(lengthField->mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        10,
        10,
        10.0f,
        10.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        2}));

    auto *editor = findFirstRecursive<cupuacu::gui::TextInput>(lengthField);
    REQUIRE(editor != nullptr);
    REQUIRE(editor->textInput("25"));

    SDL_KeyboardEvent enter{};
    enter.scancode = SDL_SCANCODE_RETURN;
    REQUIRE(editor->keyDown(enter));
    harness.statusBar->timerCallback();

    const auto &selection = state.getActiveDocumentSession().selection;
    REQUIRE(selection.isActive());
    REQUIRE(selection.getStartInt() == 100);
    REQUIRE(selection.getEndInt() == 124);
    REQUIRE(selection.getLengthInt() == 25);
    REQUIRE(startField->getValue() == "100");
    REQUIRE(endField->getValue() == "124");
    REQUIRE(lengthField->getValue() == "25");
}
