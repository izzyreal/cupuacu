#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestSdlTtfGuard.hpp"
#include "gui/Component.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/EventHandling.hpp"
#include "gui/Menu.hpp"
#include "gui/MenuBar.hpp"
#include "gui/Window.hpp"
#include "actions/Undoable.hpp"

#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <vector>

namespace
{
    class TestComponent : public cupuacu::gui::Component
    {
    public:
        explicit TestComponent(cupuacu::State *state, const char *name)
            : Component(state, name)
        {
        }

        int mouseEnterCount = 0;
        int mouseLeaveCount = 0;
        int mouseDownCount = 0;
        int mouseMoveCount = 0;
        int mouseUpCount = 0;
        int mouseWheelCount = 0;
        bool consumeMouseUp = true;

        void mouseEnter() override
        {
            ++mouseEnterCount;
        }

        void mouseLeave() override
        {
            ++mouseLeaveCount;
        }

        bool mouseMove(const cupuacu::gui::MouseEvent &) override
        {
            ++mouseMoveCount;
            return false;
        }

        bool mouseDown(const cupuacu::gui::MouseEvent &) override
        {
            ++mouseDownCount;
            return true;
        }

        bool mouseUp(const cupuacu::gui::MouseEvent &) override
        {
            ++mouseUpCount;
            return consumeMouseUp;
        }

        bool mouseWheel(const cupuacu::gui::MouseEvent &) override
        {
            ++mouseWheelCount;
            return true;
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
        int undoCount = 0;
        int redoCount = 0;

        void redo() override
        {
            ++redoCount;
        }

        void undo() override
        {
            ++undoCount;
        }

        std::string getRedoDescription() override
        {
            return description;
        }

        std::string getUndoDescription() override
        {
            return description;
        }
    };

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

TEST_CASE("Window ignores events for other windows and clears capture on mouse up",
          "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "window-test", 320, 240, SDL_WINDOW_HIDDEN);

    auto root = std::make_unique<RootComponent>(&state);
    auto *rootPtr = root.get();
    auto *captured = root->emplaceChild<TestComponent>(&state, "Captured");
    captured->setBounds(0, 0, 40, 40);
    rootPtr->setBounds(0, 0, 320, 240);
    window->setRootComponent(std::move(root));

    window->setCapturingComponent(captured);

    REQUIRE(window->handleMouseEvent(cupuacu::gui::MouseEvent{
        cupuacu::gui::UP,
        120,
        120,
        120.0f,
        120.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));
    REQUIRE(window->getCapturingComponent() == nullptr);
    REQUIRE(captured->mouseLeaveCount == 1);
    REQUIRE(captured->mouseUpCount == 1);
}

TEST_CASE("Window mouse motion updates component-under-mouse enter and leave",
          "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "window-hover", 320, 240, SDL_WINDOW_HIDDEN);

    auto root = std::make_unique<RootComponent>(&state);
    auto *left = root->emplaceChild<TestComponent>(&state, "Left");
    auto *right = root->emplaceChild<TestComponent>(&state, "Right");
    left->setBounds(0, 0, 80, 80);
    right->setBounds(100, 0, 80, 80);
    root->setBounds(0, 0, 320, 240);
    window->setRootComponent(std::move(root));

    window->updateComponentUnderMouse(10, 10);
    REQUIRE(window->getComponentUnderMouse() == left);
    REQUIRE(left->mouseEnterCount == 1);

    window->updateComponentUnderMouse(110, 10);
    REQUIRE(window->getComponentUnderMouse() == right);
    REQUIRE(left->mouseLeaveCount == 1);
    REQUIRE(right->mouseEnterCount == 1);
}

TEST_CASE("Window capture suppresses hover target changes during mouse move",
          "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "window-capture-hover", 320, 240, SDL_WINDOW_HIDDEN);

    auto root = std::make_unique<RootComponent>(&state);
    auto *left = root->emplaceChild<TestComponent>(&state, "Left");
    auto *right = root->emplaceChild<TestComponent>(&state, "Right");
    left->setBounds(0, 0, 80, 80);
    right->setBounds(100, 0, 80, 80);
    root->setBounds(0, 0, 320, 240);
    window->setRootComponent(std::move(root));

    window->updateComponentUnderMouse(10, 10);
    REQUIRE(window->getComponentUnderMouse() == left);
    REQUIRE(left->mouseEnterCount == 1);

    window->setCapturingComponent(left);
    REQUIRE(window->handleMouseEvent(cupuacu::gui::MouseEvent{
        cupuacu::gui::MOVE,
        110,
        10,
        110.0f,
        10.0f,
        100.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));

    REQUIRE(window->getComponentUnderMouse() == left);
    REQUIRE(left->mouseLeaveCount == 0);
    REQUIRE(right->mouseEnterCount == 0);
}

TEST_CASE("Clearing capture before hover refresh restores first-click targeting",
          "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "window-capture-reset", 320, 240, SDL_WINDOW_HIDDEN);

    auto root = std::make_unique<RootComponent>(&state);
    auto *left = root->emplaceChild<TestComponent>(&state, "Left");
    auto *right = root->emplaceChild<TestComponent>(&state, "Right");
    left->setBounds(0, 0, 80, 80);
    right->setBounds(100, 0, 80, 80);
    root->setBounds(0, 0, 320, 240);
    window->setRootComponent(std::move(root));

    window->updateComponentUnderMouse(10, 10);
    window->setCapturingComponent(left);

    window->setCapturingComponent(nullptr);
    window->updateComponentUnderMouse(110, 10);

    REQUIRE(window->getCapturingComponent() == nullptr);
    REQUIRE(window->getComponentUnderMouse() == right);

    REQUIRE(window->handleMouseEvent(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        110,
        10,
        110.0f,
        10.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));

    REQUIRE(right->mouseDownCount == 1);
    REQUIRE(left->mouseDownCount == 0);
}

TEST_CASE("App event handling forwards the first mouse click to an unfocused window",
          "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    state.mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            &state, &state.activeDocumentSession, "main-event-test", 320, 240,
            SDL_WINDOW_HIDDEN);

    auto *window = state.mainDocumentSessionWindow->getWindow();
    state.windows.push_back(window);
    auto root = std::make_unique<RootComponent>(&state);
    auto *target = root->emplaceChild<TestComponent>(&state, "Target");
    target->setBounds(0, 0, 80, 80);
    root->setBounds(0, 0, 320, 240);
    window->setRootComponent(std::move(root));

    REQUIRE_FALSE(window->hasFocus());

    SDL_Event button{};
    button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    button.button.windowID = window->getId();
    button.button.x = 10.0f;
    button.button.y = 10.0f;
    button.button.button = SDL_BUTTON_LEFT;
    button.button.down = true;
    button.button.clicks = 1;

    REQUIRE(cupuacu::gui::handleAppEvent(&state, &button) ==
            SDL_APP_CONTINUE);
    REQUIRE(target->mouseDownCount == 1);
}

TEST_CASE("Menu edit actions stay disabled when unavailable and run when enabled",
          "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "menu-test", 480, 240, SDL_WINDOW_HIDDEN);

    auto root = std::make_unique<RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 5);
    auto *editMenu = topLevelMenus[1];
    auto *optionsMenu = topLevelMenus[4];

    auto editSubMenus = menuChildren(editMenu);
    REQUIRE(editSubMenus.size() == 6);
    auto *cutMenu = editSubMenus[3];
    auto *pasteMenu = editSubMenus[5];

    cutMenu->mouseDown(leftMouseDown());
    pasteMenu->mouseDown(leftMouseDown());
    REQUIRE(state.undoables.empty());
    REQUIRE(state.clipboard.getFrameCount() == 0);

    state.activeDocumentSession.document.initialize(cupuacu::SampleFormat::FLOAT32,
                                                    44100, 1, 4);
    for (int64_t i = 0; i < 4; ++i)
    {
        state.activeDocumentSession.document.setSample(0, i, static_cast<float>(i),
                                                       false);
    }
    state.activeDocumentSession.selection.setValue1(1.0);
    state.activeDocumentSession.selection.setValue2(3.0);

    cutMenu->mouseDown(leftMouseDown());
    REQUIRE(state.undoables.size() == 1);
    REQUIRE(state.activeDocumentSession.document.getFrameCount() == 2);
    REQUIRE(state.clipboard.getFrameCount() == 2);

    auto optionsSubMenus = menuChildren(optionsMenu);
    REQUIRE(optionsSubMenus.size() == 1);
}

TEST_CASE("Menu open and close toggles submenu visibility", "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "menu-open", 480, 240, SDL_WINDOW_HIDDEN);

    auto root = std::make_unique<RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 5);
    auto *fileMenu = topLevelMenus[0];
    auto fileSubMenus = menuChildren(fileMenu);
    REQUIRE(fileSubMenus.size() == 2);

    REQUIRE_FALSE(fileMenu->isOpen());
    for (auto *submenu : fileSubMenus)
    {
        REQUIRE_FALSE(submenu->isVisible());
    }

    fileMenu->mouseDown(leftMouseDown());
    REQUIRE(fileMenu->isOpen());
    for (auto *submenu : fileSubMenus)
    {
        REQUIRE(submenu->isVisible());
    }

    fileMenu->mouseDown(leftMouseDown());
    REQUIRE_FALSE(fileMenu->isOpen());
    for (auto *submenu : fileSubMenus)
    {
        REQUIRE_FALSE(submenu->isVisible());
    }
}

TEST_CASE("Menu hover switches open top-level submenu when enabled by menubar",
          "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "menu-hover-switch", 480, 240, SDL_WINDOW_HIDDEN);

    auto root = std::make_unique<RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 5);
    auto *fileMenu = topLevelMenus[0];
    auto *viewMenu = topLevelMenus[2];

    auto fileSubMenus = menuChildren(fileMenu);
    auto viewSubMenus = menuChildren(viewMenu);
    REQUIRE(fileSubMenus.size() == 2);
    REQUIRE(viewSubMenus.size() == 5);

    fileMenu->mouseDown(leftMouseDown());
    REQUIRE(fileMenu->isOpen());
    REQUIRE_FALSE(viewMenu->isOpen());

    menuBar->setOpenSubMenuOnMouseOver(true);
    viewMenu->mouseEnter();

    REQUIRE_FALSE(fileMenu->isOpen());
    REQUIRE(viewMenu->isOpen());
    for (auto *submenu : fileSubMenus)
    {
        REQUIRE_FALSE(submenu->isVisible());
    }
    for (auto *submenu : viewSubMenus)
    {
        REQUIRE(submenu->isVisible());
    }
}

TEST_CASE("Menu edit undo and redo actions reflect undo stack state", "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "menu-undo-redo", 480, 240, SDL_WINDOW_HIDDEN);

    auto root = std::make_unique<RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 5);
    auto *editMenu = topLevelMenus[1];
    auto editSubMenus = menuChildren(editMenu);
    REQUIRE(editSubMenus.size() == 6);
    auto *undoMenu = editSubMenus[0];
    auto *redoMenu = editSubMenus[1];

    undoMenu->mouseDown(leftMouseDown());
    redoMenu->mouseDown(leftMouseDown());
    REQUIRE(state.undoables.empty());
    REQUIRE(state.redoables.empty());

    auto undoable =
        std::make_shared<TestUndoable>(&state, "Sample edit");
    state.addUndoable(undoable);

    undoMenu->mouseDown(leftMouseDown());
    REQUIRE(undoable->undoCount == 1);
    REQUIRE(state.undoables.empty());
    REQUIRE(state.redoables.size() == 1);

    redoMenu->mouseDown(leftMouseDown());
    REQUIRE(undoable->redoCount == 1);
    REQUIRE(state.undoables.size() == 1);
    REQUIRE(state.redoables.empty());
}

TEST_CASE("Window wheel events route to hovered component and update hover target",
          "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "window-wheel", 320, 240, SDL_WINDOW_HIDDEN);

    auto root = std::make_unique<RootComponent>(&state);
    auto *left = root->emplaceChild<TestComponent>(&state, "Left");
    auto *right = root->emplaceChild<TestComponent>(&state, "Right");
    left->setBounds(0, 0, 80, 80);
    right->setBounds(100, 0, 80, 80);
    root->setBounds(0, 0, 320, 240);
    window->setRootComponent(std::move(root));

    REQUIRE(window->handleMouseEvent(cupuacu::gui::MouseEvent{
        cupuacu::gui::WHEEL,
        110,
        10,
        110.0f,
        10.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{false, false, false},
        0,
        1.0f,
        0.0f}));

    REQUIRE(window->getComponentUnderMouse() == right);
    REQUIRE(right->mouseEnterCount == 1);
    REQUIRE(right->mouseWheelCount == 1);
    REQUIRE(left->mouseWheelCount == 0);
}

TEST_CASE("Window mouse down routes to child and starts capture", "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "window-down", 320, 240, SDL_WINDOW_HIDDEN);

    auto root = std::make_unique<RootComponent>(&state);
    auto *child = root->emplaceChild<TestComponent>(&state, "Child");
    child->setBounds(0, 0, 80, 80);
    root->setBounds(0, 0, 320, 240);
    window->setRootComponent(std::move(root));

    REQUIRE(window->handleMouseEvent(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        10,
        10,
        10.0f,
        10.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));

    REQUIRE(child->mouseDownCount == 1);
    REQUIRE(window->getCapturingComponent() == child);
}

TEST_CASE("Window mouse up without capture updates hover and dispatches to child",
          "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "window-up-no-capture", 320, 240, SDL_WINDOW_HIDDEN);

    auto root = std::make_unique<RootComponent>(&state);
    auto *child = root->emplaceChild<TestComponent>(&state, "Child");
    child->setBounds(100, 0, 80, 80);
    root->setBounds(0, 0, 320, 240);
    window->setRootComponent(std::move(root));

    REQUIRE(window->getCapturingComponent() == nullptr);
    REQUIRE(window->handleMouseEvent(cupuacu::gui::MouseEvent{
        cupuacu::gui::UP,
        110,
        10,
        110.0f,
        10.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));

    REQUIRE(window->getComponentUnderMouse() == child);
    REQUIRE(child->mouseEnterCount == 1);
    REQUIRE(child->mouseUpCount == 1);
    REQUIRE(window->getCapturingComponent() == nullptr);
}

TEST_CASE("Escape closes non-main windows", "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "esc-close", 320, 240, SDL_WINDOW_HIDDEN);

    auto root = std::make_unique<RootComponent>(&state);
    root->setBounds(0, 0, 320, 240);
    window->setRootComponent(std::move(root));

    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = window->getId();
    event.key.scancode = SDL_SCANCODE_ESCAPE;

    REQUIRE(window->handleEvent(event));
    REQUIRE_FALSE(window->isOpen());
}
