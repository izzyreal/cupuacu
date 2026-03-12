#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestSdlTtfGuard.hpp"
#include "gui/Component.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/Menu.hpp"
#include "gui/MenuBar.hpp"
#include "gui/Window.hpp"

#include <SDL3/SDL.h>

#include <memory>
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
        int mouseMoveCount = 0;
        int mouseUpCount = 0;
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

        bool mouseUp(const cupuacu::gui::MouseEvent &) override
        {
            ++mouseUpCount;
            return consumeMouseUp;
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
    REQUIRE(topLevelMenus.size() == 4);
    auto *editMenu = topLevelMenus[2];
    auto *optionsMenu = topLevelMenus[3];

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
    REQUIRE(topLevelMenus.size() == 4);
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
