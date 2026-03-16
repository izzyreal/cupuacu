#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestSdlTtfGuard.hpp"
#include "actions/Undoable.hpp"
#include "gui/Component.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/Label.hpp"
#include "gui/Menu.hpp"
#include "gui/MenuBar.hpp"
#include "gui/MenuBarPlanning.hpp"
#include "gui/Window.hpp"

#include <memory>
#include <string>
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

    class TestUndoable : public cupuacu::actions::Undoable
    {
    public:
        TestUndoable(cupuacu::State *state, std::string description)
            : Undoable(state), description(std::move(description))
        {
        }

        std::string description;

        void redo() override {}
        void undo() override {}

        std::string getRedoDescription() override
        {
            return description;
        }

        std::string getUndoDescription() override
        {
            return description;
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

    cupuacu::gui::Label *findMenuLabel(cupuacu::gui::Menu *menu)
    {
        return dynamic_cast<cupuacu::gui::Label *>(menu->getChildren().front().get());
    }
} // namespace

TEST_CASE("MenuBar planning builds dynamic undo redo labels and availability",
          "[gui]")
{
    cupuacu::State state{};

#ifdef __APPLE__
    REQUIRE(cupuacu::gui::buildUndoMenuLabel(&state) == "Undo (Cmd + Z)");
    REQUIRE(cupuacu::gui::buildRedoMenuLabel(&state) ==
            "Redo (Cmd + Shift + Z)");
#else
    REQUIRE(cupuacu::gui::buildUndoMenuLabel(&state) == "Undo (Ctrl + Z)");
    REQUIRE(cupuacu::gui::buildRedoMenuLabel(&state) ==
            "Redo (Ctrl + Shift + Z)");
#endif

    REQUIRE_FALSE(cupuacu::gui::isSelectionEditAvailable(&state));
    REQUIRE_FALSE(cupuacu::gui::isPasteAvailable(&state));

    state.activeDocumentSession.document.initialize(cupuacu::SampleFormat::FLOAT32,
                                                    44100, 1, 4);
    state.activeDocumentSession.selection.setHighest(4.0);
    state.activeDocumentSession.selection.setValue1(1.0);
    state.activeDocumentSession.selection.setValue2(3.0);
    state.clipboard.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, 2);

    REQUIRE(cupuacu::gui::isSelectionEditAvailable(&state));
    REQUIRE(cupuacu::gui::isPasteAvailable(&state));

    auto undoable = std::make_shared<TestUndoable>(&state, "Sample edit");
    state.addUndoable(undoable);

#ifdef __APPLE__
    REQUIRE(cupuacu::gui::buildUndoMenuLabel(&state) ==
            "Undo Sample edit (Cmd + Z)");
#else
    REQUIRE(cupuacu::gui::buildUndoMenuLabel(&state) ==
            "Undo Sample edit (Ctrl + Z)");
#endif

    state.undo();

#ifdef __APPLE__
    REQUIRE(cupuacu::gui::buildRedoMenuLabel(&state) ==
            "Redo Sample edit (Cmd + Shift + Z)");
#else
    REQUIRE(cupuacu::gui::buildRedoMenuLabel(&state) ==
            "Redo Sample edit (Ctrl + Shift + Z)");
#endif
}

TEST_CASE("MenuBar integration reflects dynamic undo label text", "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "menu-bar-planning", 480, 240, SDL_WINDOW_HIDDEN);

    auto root = std::make_unique<RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 5);
    for (auto *menu : topLevelMenus)
    {
        menu->onDraw(window->getRenderer());
    }
    REQUIRE(findMenuLabel(topLevelMenus[0])->getText() == "File");
    REQUIRE(findMenuLabel(topLevelMenus[1])->getText() == "Edit");
    REQUIRE(findMenuLabel(topLevelMenus[2])->getText() == "View");
    REQUIRE(findMenuLabel(topLevelMenus[3])->getText() == "Effects");
    REQUIRE(findMenuLabel(topLevelMenus[4])->getText() == "Options");

    auto *editMenu = topLevelMenus[1];
    auto editSubMenus = menuChildren(editMenu);
    REQUIRE(editSubMenus.size() == 6);
    auto *undoMenu = editSubMenus[0];

    undoMenu->onDraw(window->getRenderer());
    auto *label = findMenuLabel(undoMenu);
    REQUIRE(label != nullptr);
#ifdef __APPLE__
    REQUIRE(label->getText() == "Undo (Cmd + Z)");
#else
    REQUIRE(label->getText() == "Undo (Ctrl + Z)");
#endif

    auto undoable = std::make_shared<TestUndoable>(&state, "Cut");
    state.addUndoable(undoable);
    undoMenu->onDraw(window->getRenderer());
#ifdef __APPLE__
    REQUIRE(label->getText() == "Undo Cut (Cmd + Z)");
#else
    REQUIRE(label->getText() == "Undo Cut (Ctrl + Z)");
#endif
}
