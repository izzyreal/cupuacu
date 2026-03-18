#include <catch2/catch_test_macros.hpp>

#include "IntegrationTestHelpers.hpp"

#include "State.hpp"
#include "Paths.hpp"
#include "actions/Undoable.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/DropdownMenu.hpp"
#include "gui/EventHandling.hpp"
#include "gui/Label.hpp"
#include "gui/Menu.hpp"
#include "gui/MenuBar.hpp"
#include "gui/Window.hpp"
#include "persistence/AudioDevicePropertiesPersistence.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
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

    class FocusableTestComponent : public TestComponent
    {
    public:
        using TestComponent::TestComponent;

        int focusGainedCount = 0;
        int focusLostCount = 0;
        int keyDownCount = 0;
        int textInputCount = 0;
        SDL_Scancode lastScancode = SDL_SCANCODE_UNKNOWN;
        std::string lastText;

        bool acceptsKeyboardFocus() const override
        {
            return true;
        }

        void focusGained() override
        {
            ++focusGainedCount;
        }

        void focusLost() override
        {
            ++focusLostCount;
        }

        bool keyDown(const SDL_KeyboardEvent &event) override
        {
            ++keyDownCount;
            lastScancode = event.scancode;
            return true;
        }

        bool textInput(const std::string_view text) override
        {
            ++textInputCount;
            lastText = std::string(text);
            return true;
        }
    };

    class CloseRequestingComponent : public TestComponent
    {
    public:
        using TestComponent::TestComponent;

        bool mouseDown(const cupuacu::gui::MouseEvent &event) override
        {
            (void)event;
            ++mouseDownCount;
            if (auto *ownerWindow = getWindow())
            {
                ownerWindow->requestClose();
            }
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

    class TestPaths : public cupuacu::Paths
    {
    public:
        explicit TestPaths(std::filesystem::path rootToUse)
            : root(std::move(rootToUse))
        {
        }

    protected:
        std::filesystem::path appConfigHome() const override
        {
            return root;
        }

    private:
        std::filesystem::path root;
    };

    class ScopedConfigCleanup
    {
    public:
        explicit ScopedConfigCleanup(std::filesystem::path rootToUse)
            : root(std::move(rootToUse))
        {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }

        ~ScopedConfigCleanup()
        {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }

    private:
        std::filesystem::path root;
    };

    class ScopedResolverOverride
    {
    public:
        explicit ScopedResolverOverride(
            cupuacu::persistence::AudioDevicePropertiesPersistence::Resolver
                resolver)
        {
            cupuacu::persistence::AudioDevicePropertiesPersistence::
                setResolverForTesting(std::move(resolver));
        }

        ~ScopedResolverOverride()
        {
            cupuacu::persistence::AudioDevicePropertiesPersistence::
                resetResolverForTesting();
        }
    };

    template <typename T>
    void collectChildrenRecursive(cupuacu::gui::Component *root,
                                  std::vector<T *> &out)
    {
        if (!root)
        {
            return;
        }

        if (auto *match = dynamic_cast<T *>(root))
        {
            out.push_back(match);
        }

        for (const auto &child : root->getChildren())
        {
            collectChildrenRecursive<T>(child.get(), out);
        }
    }
} // namespace

TEST_CASE("Window integration clears capture on mouse up and updates hover",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "window-test", 320, 240, SDL_WINDOW_HIDDEN);

    auto root = std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *captured = root->emplaceChild<TestComponent>(&state, "Captured");
    captured->setBounds(0, 0, 40, 40);
    root->setBounds(0, 0, 320, 240);
    window->setRootComponent(std::move(root));
    window->setCapturingComponent(captured);

    REQUIRE(window->handleMouseEvent(cupuacu::gui::MouseEvent{
        cupuacu::gui::UP, 120, 120, 120.0f, 120.0f, 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{true, false, false}, 1}));
    REQUIRE(window->getCapturingComponent() == nullptr);
    REQUIRE(captured->mouseLeaveCount == 1);
    REQUIRE(captured->mouseUpCount == 1);
}

TEST_CASE("Window integration routes wheel events to hovered component",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "window-wheel", 320, 240, SDL_WINDOW_HIDDEN);

    auto root = std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *left = root->emplaceChild<TestComponent>(&state, "Left");
    auto *right = root->emplaceChild<TestComponent>(&state, "Right");
    left->setBounds(0, 0, 80, 80);
    right->setBounds(100, 0, 80, 80);
    root->setBounds(0, 0, 320, 240);
    window->setRootComponent(std::move(root));

    REQUIRE(window->handleMouseEvent(cupuacu::gui::MouseEvent{
        cupuacu::gui::WHEEL, 110, 10, 110.0f, 10.0f, 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{false, false, false}, 0, 1.0f, 0.0f}));

    REQUIRE(window->getComponentUnderMouse() == right);
    REQUIRE(right->mouseEnterCount == 1);
    REQUIRE(right->mouseWheelCount == 1);
    REQUIRE(left->mouseWheelCount == 0);
}

TEST_CASE("Menu integration opens submenus and switches siblings on hover",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "menu-hover-switch", 480, 240, SDL_WINDOW_HIDDEN);

    auto root = std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = cupuacu::test::integration::menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 5);
    auto *fileMenu = topLevelMenus[0];
    auto *viewMenu = topLevelMenus[2];

    auto fileSubMenus = cupuacu::test::integration::menuChildren(fileMenu);
    auto viewSubMenus = cupuacu::test::integration::menuChildren(viewMenu);
    REQUIRE(fileSubMenus.size() == 2);
    REQUIRE(viewSubMenus.size() == 5);

    fileMenu->mouseDown(cupuacu::test::integration::leftMouseDown());
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

TEST_CASE("Menu integration undo and redo actions reflect undo stack state",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "menu-undo-redo", 480, 240, SDL_WINDOW_HIDDEN);

    auto root = std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = cupuacu::test::integration::menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 5);
    auto *editMenu = topLevelMenus[1];
    auto editSubMenus = cupuacu::test::integration::menuChildren(editMenu);
    REQUIRE(editSubMenus.size() == 6);
    auto *undoMenu = editSubMenus[0];
    auto *redoMenu = editSubMenus[1];

    auto undoable = std::make_shared<TestUndoable>(&state, "Sample edit");
    state.addUndoable(undoable);

    undoMenu->mouseDown(cupuacu::test::integration::leftMouseDown());
    REQUIRE(undoable->undoCount == 1);
    REQUIRE(state.undoables.empty());
    REQUIRE(state.redoables.size() == 1);

    redoMenu->mouseDown(cupuacu::test::integration::leftMouseDown());
    REQUIRE(undoable->redoCount == 1);
    REQUIRE(state.undoables.size() == 1);
    REQUIRE(state.redoables.empty());
}

TEST_CASE("Options menu integration opens device properties window once",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto sessionUi =
        cupuacu::test::integration::createSessionUi(&state, 512, false);
    REQUIRE(state.mainDocumentSessionWindow != nullptr);

    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "options-device-properties", 480, 240, SDL_WINDOW_HIDDEN);

    auto root =
        std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = cupuacu::test::integration::menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 5);
    auto *optionsMenu = topLevelMenus[4];

    auto optionEntries = cupuacu::test::integration::menuChildren(optionsMenu);
    REQUIRE(optionEntries.size() == 1);
    auto *devicePropertiesEntry = optionEntries[0];

    REQUIRE(state.devicePropertiesWindow == nullptr);
    const auto initialWindowCount = state.windows.size();

    optionsMenu->mouseDown(cupuacu::test::integration::leftMouseDown());
    REQUIRE(optionsMenu->isOpen());

    devicePropertiesEntry->mouseDown(cupuacu::test::integration::leftMouseDown());
    REQUIRE(state.devicePropertiesWindow != nullptr);
    REQUIRE(state.devicePropertiesWindow->isOpen());
    REQUIRE(state.windows.size() == initialWindowCount + 1);

    std::vector<cupuacu::gui::DropdownMenu *> dropdowns;
    std::vector<cupuacu::gui::Label *> labels;
    collectChildrenRecursive(state.devicePropertiesWindow->getWindow()->getRootComponent(),
                             dropdowns);
    collectChildrenRecursive(state.devicePropertiesWindow->getWindow()->getRootComponent(),
                             labels);

    REQUIRE(dropdowns.size() == 3);
    REQUIRE(labels.size() >= 3);
    for (auto *dropdown : dropdowns)
    {
        REQUIRE(dropdown->getSelectedIndex() >= 0);
        REQUIRE(dropdown->getHeight() >= dropdown->getRowHeight());
    }

    auto *openedWindow = state.devicePropertiesWindow.get();
    optionsMenu->mouseDown(cupuacu::test::integration::leftMouseDown());
    devicePropertiesEntry->mouseDown(cupuacu::test::integration::leftMouseDown());
    REQUIRE(state.devicePropertiesWindow.get() == openedWindow);
    REQUIRE(state.windows.size() == initialWindowCount + 1);

    state.devicePropertiesWindow.reset();
    REQUIRE(state.windows.size() == initialWindowCount);
}

TEST_CASE("Secondary window integration handles escape-close callback",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "secondary-window-escape", 320, 240, SDL_WINDOW_HIDDEN);

    int onCloseCount = 0;
    window->setOnClose([&]() { ++onCloseCount; });

    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = window->getId();
    event.key.scancode = SDL_SCANCODE_ESCAPE;

    REQUIRE(window->handleEvent(event));
    REQUIRE(onCloseCount == 1);
    REQUIRE_FALSE(window->isOpen());
}

TEST_CASE("Window integration forwards key and text input to focused component",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "window-focused-input", 320, 240, SDL_WINDOW_HIDDEN);

    auto root =
        std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *focused =
        root->emplaceChild<FocusableTestComponent>(&state, "Focused");
    focused->setBounds(0, 0, 80, 40);
    root->setBounds(0, 0, 320, 240);
    window->setRootComponent(std::move(root));
    window->setFocusedComponent(focused);

    SDL_Event keyEvent{};
    keyEvent.type = SDL_EVENT_KEY_DOWN;
    keyEvent.key.windowID = window->getId();
    keyEvent.key.scancode = SDL_SCANCODE_A;

    SDL_Event textEvent{};
    textEvent.type = SDL_EVENT_TEXT_INPUT;
    textEvent.text.windowID = window->getId();
    textEvent.text.text = "a";

    REQUIRE(window->handleEvent(keyEvent));
    REQUIRE(window->handleEvent(textEvent));
    REQUIRE(focused->focusGainedCount == 1);
    REQUIRE(focused->keyDownCount == 1);
    REQUIRE(focused->lastScancode == SDL_SCANCODE_A);
    REQUIRE(focused->textInputCount == 1);
    REQUIRE(focused->lastText == "a");
}

TEST_CASE("Window integration keeps the main document window open on escape",
          "[integration]")
{
    cupuacu::State state{};
    auto sessionUi =
        cupuacu::test::integration::createSessionUi(&state, 512, false);

    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = state.mainDocumentSessionWindow->getWindow()->getId();
    event.key.scancode = SDL_SCANCODE_ESCAPE;

    REQUIRE_FALSE(
        state.mainDocumentSessionWindow->getWindow()->handleEvent(event));
    REQUIRE(state.mainDocumentSessionWindow->getWindow()->isOpen());
}

TEST_CASE("Window integration honors requestClose queued during mouse dispatch",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "window-request-close", 320, 240, SDL_WINDOW_HIDDEN);

    int onCloseCount = 0;
    window->setOnClose([&]() { ++onCloseCount; });

    auto root =
        std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *closeRequester =
        root->emplaceChild<CloseRequestingComponent>(&state, "CloseRequester");
    closeRequester->setBounds(0, 0, 80, 80);
    root->setBounds(0, 0, 320, 240);
    window->setRootComponent(std::move(root));

    REQUIRE(window->handleMouseEvent(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN, 10, 10, 10.0f, 10.0f, 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{true, false, false}, 1}));
    REQUIRE(closeRequester->mouseDownCount == 1);
    REQUIRE(onCloseCount == 1);
    REQUIRE_FALSE(window->isOpen());
}

TEST_CASE("Device properties integration persists normalized selection when reopened from an invalid stored selection",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    const auto configRoot =
        std::filesystem::temp_directory_path() /
        "cupuacu-device-properties-integration";
    ScopedConfigCleanup cleanup(configRoot);

    cupuacu::State state{};
    state.paths = std::make_unique<TestPaths>(configRoot);
    auto sessionUi =
        cupuacu::test::integration::createSessionUi(&state, 512, false);
    state.audioDevices = std::make_shared<cupuacu::audio::AudioDevices>(false);
    REQUIRE(state.audioDevices != nullptr);

    cupuacu::audio::AudioDevices::DeviceSelection invalidSelection;
    invalidSelection.hostApiIndex = 999;
    invalidSelection.outputDeviceIndex = 998;
    invalidSelection.inputDeviceIndex = 997;
    REQUIRE(state.audioDevices->setDeviceSelection(invalidSelection));

    ScopedResolverOverride resolverOverride(
        cupuacu::persistence::AudioDevicePropertiesPersistence::Resolver{
            .resolveHostApiName =
                [](const int hostApiIndex)
            {
                return "host:" + std::to_string(hostApiIndex);
            },
            .resolveDeviceName =
                [](const int deviceIndex)
            {
                return "device:" + std::to_string(deviceIndex);
            },
            .resolveHostApiIndex =
                [](const std::string &hostApiName)
            {
                return hostApiName.rfind("host:", 0) == 0
                           ? std::stoi(hostApiName.substr(5))
                           : -1;
            },
            .resolveDeviceIndex =
                [](const std::string &deviceName, const bool, const int)
            {
                return deviceName.rfind("device:", 0) == 0
                           ? std::stoi(deviceName.substr(7))
                           : -1;
            }});

    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "options-device-properties-persist", 480, 240,
        SDL_WINDOW_HIDDEN);

    auto root =
        std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = cupuacu::test::integration::menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 5);
    auto *optionsMenu = topLevelMenus[4];
    auto optionEntries = cupuacu::test::integration::menuChildren(optionsMenu);
    REQUIRE(optionEntries.size() == 1);

    REQUIRE(state.devicePropertiesWindow == nullptr);
    REQUIRE(optionsMenu->mouseDown(cupuacu::test::integration::leftMouseDown()));
    REQUIRE(optionEntries[0]->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.devicePropertiesWindow != nullptr);
    REQUIRE(state.devicePropertiesWindow->isOpen());

    const auto normalizedSelection = state.audioDevices->getDeviceSelection();
    REQUIRE(normalizedSelection != invalidSelection);

    const auto persistedPath = state.paths->audioDevicePropertiesPath();
    REQUIRE(std::filesystem::exists(persistedPath));

    nlohmann::json json;
    {
        std::ifstream in(persistedPath, std::ios::binary);
        REQUIRE(in.good());
        in >> json;
    }

    REQUIRE(json.at("hostApiName").get<std::string>() ==
            "host:" + std::to_string(normalizedSelection.hostApiIndex));
    REQUIRE(json.at("outputDeviceName").get<std::string>() ==
            "device:" + std::to_string(normalizedSelection.outputDeviceIndex));
    REQUIRE(json.at("inputDeviceName").get<std::string>() ==
            "device:" + std::to_string(normalizedSelection.inputDeviceIndex));
}

TEST_CASE("Options menu integration replaces a closed device properties window instance",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto sessionUi =
        cupuacu::test::integration::createSessionUi(&state, 512, false);

    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "options-device-properties-reopen", 480, 240,
        SDL_WINDOW_HIDDEN);

    auto root =
        std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = cupuacu::test::integration::menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 5);
    auto *optionsMenu = topLevelMenus[4];
    auto optionEntries = cupuacu::test::integration::menuChildren(optionsMenu);
    REQUIRE(optionEntries.size() == 1);

    REQUIRE(optionsMenu->mouseDown(cupuacu::test::integration::leftMouseDown()));
    REQUIRE(optionEntries[0]->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.devicePropertiesWindow != nullptr);
    auto *firstWindow = state.devicePropertiesWindow.get();

    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = firstWindow->getWindow()->getId();
    event.key.scancode = SDL_SCANCODE_ESCAPE;

    REQUIRE(firstWindow->getWindow()->handleEvent(event));
    REQUIRE_FALSE(firstWindow->isOpen());

    REQUIRE(optionsMenu->mouseDown(cupuacu::test::integration::leftMouseDown()));
    REQUIRE(optionEntries[0]->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.devicePropertiesWindow != nullptr);
    REQUIRE(state.devicePropertiesWindow.get() != firstWindow);
    REQUIRE(state.devicePropertiesWindow->isOpen());
}

TEST_CASE("Device properties integration refreshes layout when pixel scale changes",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto sessionUi =
        cupuacu::test::integration::createSessionUi(&state, 512, false);

    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "options-device-properties-refresh", 480, 240,
        SDL_WINDOW_HIDDEN);

    auto root =
        std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = cupuacu::test::integration::menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 5);
    auto *optionsMenu = topLevelMenus[4];
    auto optionEntries = cupuacu::test::integration::menuChildren(optionsMenu);
    REQUIRE(optionEntries.size() == 1);

    REQUIRE(optionsMenu->mouseDown(cupuacu::test::integration::leftMouseDown()));
    REQUIRE(optionEntries[0]->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.devicePropertiesWindow != nullptr);

    std::vector<cupuacu::gui::DropdownMenu *> dropdowns;
    collectChildrenRecursive(
        state.devicePropertiesWindow->getWindow()->getRootComponent(), dropdowns);
    REQUIRE(dropdowns.size() == 3);

    const int originalHeight = dropdowns[0]->getHeight();
    const int originalY = dropdowns[1]->getYPos();

    state.pixelScale = 2;
    state.devicePropertiesWindow->getWindow()->refreshForScaleOrResize();

    REQUIRE(dropdowns[0]->getHeight() != originalHeight);
    REQUIRE(dropdowns[1]->getYPos() != originalY);
}
