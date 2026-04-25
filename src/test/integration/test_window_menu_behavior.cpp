#include <catch2/catch_test_macros.hpp>

#include "IntegrationTestHelpers.hpp"

#include "State.hpp"
#include "actions/DocumentLifecycle.hpp"
#include "actions/Undoable.hpp"
#include "file/SndfilePath.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/DisplaySettingsWindow.hpp"
#include "gui/DropdownMenu.hpp"
#include "gui/EventHandling.hpp"
#include "gui/Label.hpp"
#include "gui/Menu.hpp"
#include "gui/MenuBar.hpp"
#include "gui/UiScale.hpp"
#include "gui/Window.hpp"
#include "persistence/AudioDevicePropertiesPersistence.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <sndfile.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
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

        [[nodiscard]] cupuacu::file::OverwritePreservationMutation
        overwritePreservationMutation() const override
        {
            return cupuacu::file::OverwritePreservationMutationHelper::compatible();
        }
    };

    cupuacu::gui::MouseEvent leftMouseDownAt(const int x, const int y)
    {
        return cupuacu::gui::MouseEvent{
            cupuacu::gui::DOWN, x, y, static_cast<float>(x),
            static_cast<float>(y), 0.0f, 0.0f,
            cupuacu::gui::MouseButtonState{true, false, false}, 1};
    }

    cupuacu::gui::MouseEvent leftMouseUpAt(const int x, const int y)
    {
        return cupuacu::gui::MouseEvent{
            cupuacu::gui::UP, x, y, static_cast<float>(x), static_cast<float>(y),
            0.0f, 0.0f, cupuacu::gui::MouseButtonState{true, false, false}, 1};
    }

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

    class ScopedDirCleanup
    {
    public:
        explicit ScopedDirCleanup(std::filesystem::path rootDir)
            : root(std::move(rootDir))
        {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
            std::filesystem::create_directories(root, ec);
        }

        ~ScopedDirCleanup()
        {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }

        const std::filesystem::path &path() const
        {
            return root;
        }

    private:
        std::filesystem::path root;
    };

    std::filesystem::path makeUniqueTempDir(const std::string &prefix)
    {
        const auto tempRoot = std::filesystem::temp_directory_path();
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dis;

        for (int attempt = 0; attempt < 32; ++attempt)
        {
            const auto now =
                std::chrono::high_resolution_clock::now().time_since_epoch();
            const auto tick =
                static_cast<uint64_t>(std::chrono::duration_cast<
                                          std::chrono::nanoseconds>(now)
                                          .count());
            const auto candidate =
                tempRoot / (prefix + "-" + std::to_string(tick) + "-" +
                            std::to_string(dis(gen)));
            std::error_code ec;
            if (!std::filesystem::exists(candidate, ec))
            {
                return candidate;
            }
        }

        return tempRoot / (prefix + "-fallback");
    }

    void writeTestWav(const std::filesystem::path &path, const int sampleRate,
                      const int channels,
                      const std::vector<float> &interleavedFrames)
    {
        REQUIRE(channels > 0);
        REQUIRE(interleavedFrames.size() % channels == 0);

        SF_INFO info{};
        info.samplerate = sampleRate;
        info.channels = channels;
        info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

        SNDFILE *file = cupuacu::file::openSndfile(path, SFM_WRITE, &info);
        REQUIRE(file != nullptr);

        const sf_count_t frameCount =
            static_cast<sf_count_t>(interleavedFrames.size() / channels);
        const sf_count_t written =
            sf_writef_float(file, interleavedFrames.data(), frameCount);

        sf_close(file);
        REQUIRE(written == frameCount);
    }

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

    cupuacu::test::StateWithTestPaths state{};
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

    cupuacu::test::StateWithTestPaths state{};
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

    cupuacu::test::StateWithTestPaths state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "menu-hover-switch", 480, 240, SDL_WINDOW_HIDDEN);

    auto root = std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = cupuacu::test::integration::menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 6);
    auto *fileMenu = topLevelMenus[0];
    auto *viewMenu = topLevelMenus[2];

    auto fileSubMenus = cupuacu::test::integration::menuChildren(fileMenu);
    auto viewSubMenus = cupuacu::test::integration::menuChildren(viewMenu);
    REQUIRE(fileSubMenus.size() == 9);
    REQUIRE(viewSubMenus.size() == 6);

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

    cupuacu::test::StateWithTestPaths state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "menu-undo-redo", 480, 240, SDL_WINDOW_HIDDEN);

    auto root = std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = cupuacu::test::integration::menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 6);
    auto *editMenu = topLevelMenus[1];
    auto editSubMenus = cupuacu::test::integration::menuChildren(editMenu);
    REQUIRE(editSubMenus.size() == 9);
    auto *undoMenu = editSubMenus[0];
    auto *redoMenu = editSubMenus[1];

    auto undoable = std::make_shared<TestUndoable>(&state, "Sample edit");
    state.addUndoable(undoable);

    undoMenu->mouseDown(cupuacu::test::integration::leftMouseDown());
    REQUIRE(undoable->undoCount == 1);
    REQUIRE(state.getActiveUndoables().empty());
    REQUIRE(state.getActiveRedoables().size() == 1);

    redoMenu->mouseDown(cupuacu::test::integration::leftMouseDown());
    REQUIRE(undoable->redoCount == 1);
    REQUIRE(state.getActiveUndoables().size() == 1);
    REQUIRE(state.getActiveRedoables().empty());
}

TEST_CASE("Options menu integration opens audio options in a shared options window once",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::test::StateWithTestPaths state{};
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
    REQUIRE(topLevelMenus.size() == 6);
    auto *optionsMenu = topLevelMenus[5];

    auto optionEntries = cupuacu::test::integration::menuChildren(optionsMenu);
    REQUIRE(optionEntries.size() == 3);
    auto *audioEntry = optionEntries[1];

    REQUIRE(state.optionsWindow == nullptr);
    const auto initialWindowCount = state.windows.size();

    optionsMenu->mouseDown(cupuacu::test::integration::leftMouseDown());
    REQUIRE(optionsMenu->isOpen());

    audioEntry->mouseDown(cupuacu::test::integration::leftMouseDown());
    REQUIRE(state.optionsWindow != nullptr);
    REQUIRE(state.optionsWindow->isOpen());
    REQUIRE(state.windows.size() == initialWindowCount + 1);

    auto *audioPane = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::Component>(
        state.optionsWindow->getWindow()->getRootComponent(),
        "OptionsAudioPane");
    REQUIRE(audioPane != nullptr);

    std::vector<cupuacu::gui::DropdownMenu *> dropdowns;
    std::vector<cupuacu::gui::Label *> labels;
    collectChildrenRecursive(audioPane,
                             dropdowns);
    collectChildrenRecursive(audioPane,
                             labels);

    REQUIRE(dropdowns.size() == 3);
    REQUIRE(labels.size() >= 3);
    for (auto *dropdown : dropdowns)
    {
        REQUIRE(dropdown->getSelectedIndex() >= 0);
        REQUIRE(dropdown->getHeight() >= dropdown->getRowHeight());
    }

    auto *openedWindow = state.optionsWindow.get();
    optionsMenu->mouseDown(cupuacu::test::integration::leftMouseDown());
    audioEntry->mouseDown(cupuacu::test::integration::leftMouseDown());
    REQUIRE(state.optionsWindow.get() == openedWindow);
    REQUIRE(state.windows.size() == initialWindowCount + 1);

    state.optionsWindow.reset();
    REQUIRE(state.windows.size() == initialWindowCount);
}

TEST_CASE("Options menu integration opens display options in a shared options window once",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::test::StateWithTestPaths state{};
    auto sessionUi =
        cupuacu::test::integration::createSessionUi(&state, 512, false);
    REQUIRE(state.mainDocumentSessionWindow != nullptr);

    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "options-display-settings", 480, 240, SDL_WINDOW_HIDDEN);

    auto root =
        std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = cupuacu::test::integration::menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 6);
    auto *optionsMenu = topLevelMenus[5];

    auto optionEntries = cupuacu::test::integration::menuChildren(optionsMenu);
    REQUIRE(optionEntries.size() == 3);
    auto *displayEntry = optionEntries[2];

    REQUIRE(state.optionsWindow == nullptr);
    const auto initialWindowCount = state.windows.size();

    optionsMenu->mouseDown(cupuacu::test::integration::leftMouseDown());
    REQUIRE(optionsMenu->isOpen());

    displayEntry->mouseDown(cupuacu::test::integration::leftMouseDown());
    REQUIRE(state.optionsWindow != nullptr);
    REQUIRE(state.optionsWindow->isOpen());
    REQUIRE(state.windows.size() == initialWindowCount + 1);
    REQUIRE(state.optionsWindow->getSelectedSection() ==
            cupuacu::gui::OptionsSection::Display);

    auto *displayPane = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::Component>(
        state.optionsWindow->getWindow()->getRootComponent(),
        "OptionsDisplayPane");
    REQUIRE(displayPane != nullptr);

    std::vector<cupuacu::gui::DropdownMenu *> dropdowns;
    collectChildrenRecursive(displayPane, dropdowns);
    REQUIRE(dropdowns.size() == 2);
    REQUIRE(dropdowns[0]->getSelectedIndex() >= 0);
    REQUIRE(dropdowns[1]->getSelectedIndex() >= 0);

    auto *openedWindow = state.optionsWindow.get();
    optionsMenu->mouseDown(cupuacu::test::integration::leftMouseDown());
    displayEntry->mouseDown(cupuacu::test::integration::leftMouseDown());
    REQUIRE(state.optionsWindow.get() == openedWindow);
    REQUIRE(state.windows.size() == initialWindowCount + 1);

    state.optionsWindow.reset();
    REQUIRE(state.windows.size() == initialWindowCount);
}

TEST_CASE("Secondary window integration handles escape-close callback",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::test::StateWithTestPaths state{};
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

TEST_CASE("Secondary window integration handles escape-close through cancel action immediately",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::test::StateWithTestPaths state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "secondary-window-escape-cancel-action", 320, 240,
        SDL_WINDOW_HIDDEN);

    int cancelActionCount = 0;
    int onCloseCount = 0;
    window->setCancelAction(
        [&]()
        {
            ++cancelActionCount;
            window->requestClose();
        });
    window->setOnClose([&]() { ++onCloseCount; });

    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = window->getId();
    event.key.scancode = SDL_SCANCODE_ESCAPE;

    REQUIRE(window->handleEvent(event));
    REQUIRE(cancelActionCount == 1);
    REQUIRE(onCloseCount == 1);
    REQUIRE_FALSE(window->isOpen());
}

TEST_CASE("Window integration forwards key and text input to focused component",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::test::StateWithTestPaths state{};
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
    cupuacu::test::StateWithTestPaths state{};
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

    cupuacu::test::StateWithTestPaths state{};
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
        cupuacu::test::makeUniqueTestRoot(
            "device-properties-integration");
    ScopedConfigCleanup cleanup(configRoot);

    cupuacu::test::StateWithTestPaths state{configRoot};
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
    REQUIRE(topLevelMenus.size() == 6);
    auto *optionsMenu = topLevelMenus[5];
    auto optionEntries = cupuacu::test::integration::menuChildren(optionsMenu);
    REQUIRE(optionEntries.size() == 3);

    REQUIRE(state.optionsWindow == nullptr);
    REQUIRE(optionsMenu->mouseDown(cupuacu::test::integration::leftMouseDown()));
    REQUIRE(optionEntries[1]->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.optionsWindow != nullptr);
    REQUIRE(state.optionsWindow->isOpen());

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

TEST_CASE("Options menu integration replaces a closed options window instance",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::test::StateWithTestPaths state{};
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
    REQUIRE(topLevelMenus.size() == 6);
    auto *optionsMenu = topLevelMenus[5];
    auto optionEntries = cupuacu::test::integration::menuChildren(optionsMenu);
    REQUIRE(optionEntries.size() == 3);

    REQUIRE(optionsMenu->mouseDown(cupuacu::test::integration::leftMouseDown()));
    REQUIRE(optionEntries[1]->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.optionsWindow != nullptr);
    auto *firstWindow = state.optionsWindow.get();

    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = firstWindow->getWindow()->getId();
    event.key.scancode = SDL_SCANCODE_ESCAPE;

    REQUIRE(firstWindow->getWindow()->handleEvent(event));
    REQUIRE_FALSE(firstWindow->isOpen());

    REQUIRE(optionsMenu->mouseDown(cupuacu::test::integration::leftMouseDown()));
    REQUIRE(optionEntries[0]->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.optionsWindow != nullptr);
    REQUIRE(state.optionsWindow.get() != firstWindow);
    REQUIRE(state.optionsWindow->isOpen());
}

TEST_CASE("Options window integration reopens on the last selected section from All options",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::test::StateWithTestPaths state{};
    auto sessionUi =
        cupuacu::test::integration::createSessionUi(&state, 512, false);

    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "options-last-section", 480, 240, SDL_WINDOW_HIDDEN);

    auto root =
        std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = cupuacu::test::integration::menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 6);
    auto *optionsMenu = topLevelMenus[5];
    auto optionEntries = cupuacu::test::integration::menuChildren(optionsMenu);
    REQUIRE(optionEntries.size() == 3);

    REQUIRE(optionsMenu->mouseDown(cupuacu::test::integration::leftMouseDown()));
    REQUIRE(optionEntries[2]->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.optionsWindow != nullptr);
    REQUIRE(state.optionsWindow->getSelectedSection() ==
            cupuacu::gui::OptionsSection::Display);

    auto *openedWindow = state.optionsWindow.get();
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = openedWindow->getWindow()->getId();
    event.key.scancode = SDL_SCANCODE_ESCAPE;

    REQUIRE(openedWindow->getWindow()->handleEvent(event));
    REQUIRE_FALSE(openedWindow->isOpen());

    REQUIRE(optionsMenu->mouseDown(cupuacu::test::integration::leftMouseDown()));
    REQUIRE(optionEntries[0]->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.optionsWindow != nullptr);
    REQUIRE(state.optionsWindow->isOpen());
    REQUIRE(state.optionsWindow->getSelectedSection() ==
            cupuacu::gui::OptionsSection::Display);
}

TEST_CASE("Device properties integration refreshes layout when pixel scale changes",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::test::StateWithTestPaths state{};
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
    REQUIRE(topLevelMenus.size() == 6);
    auto *optionsMenu = topLevelMenus[5];
    auto optionEntries = cupuacu::test::integration::menuChildren(optionsMenu);
    REQUIRE(optionEntries.size() == 3);

    REQUIRE(optionsMenu->mouseDown(cupuacu::test::integration::leftMouseDown()));
    REQUIRE(optionEntries[1]->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.optionsWindow != nullptr);

    auto *audioPane = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::Component>(
        state.optionsWindow->getWindow()->getRootComponent(),
        "OptionsAudioPane");
    auto *audioButton = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::TextButton>(
        state.optionsWindow->getWindow()->getRootComponent(),
        "TextButton:Audio");
    REQUIRE(audioPane != nullptr);
    REQUIRE(audioButton != nullptr);
    std::vector<cupuacu::gui::DropdownMenu *> dropdowns;
    collectChildrenRecursive(audioPane, dropdowns);
    REQUIRE(dropdowns.size() == 3);

    const int originalHeight = dropdowns[0]->getHeight();
    const int originalY = dropdowns[1]->getYPos();
    const int originalButtonHeight = audioButton->getHeight();
    const int originalButtonWidth = audioButton->getWidth();

    state.pixelScale = 2;
    state.optionsWindow->getWindow()->refreshForScaleOrResize();

    REQUIRE(dropdowns[0]->getHeight() != originalHeight);
    REQUIRE(dropdowns[1]->getYPos() != originalY);
    REQUIRE(audioButton->getHeight() != originalButtonHeight);
    REQUIRE(audioButton->getWidth() != originalButtonWidth);
}


TEST_CASE("Dropdown integration keeps only one dropdown open and closes on outside clicks",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::test::StateWithTestPaths state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "dropdown-exclusive", 320, 240, SDL_WINDOW_HIDDEN);

    auto root =
        std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    root->setBounds(0, 0, 320, 240);
    auto *first = root->emplaceChild<cupuacu::gui::DropdownMenu>(&state);
    auto *second = root->emplaceChild<cupuacu::gui::DropdownMenu>(&state);
    first->setBounds(20, 20, 180, 30);
    second->setBounds(20, 140, 180, 30);
    first->setItems({"Alpha", "Beta", "Gamma"});
    second->setItems({"One", "Two", "Three"});
    first->setCollapsedHeight(30);
    second->setCollapsedHeight(30);
    window->setRootComponent(std::move(root));

    REQUIRE(window->handleMouseEvent(leftMouseDownAt(40, 35)));
    REQUIRE(window->handleMouseEvent(leftMouseUpAt(40, 35)));
    REQUIRE(first->isExpanded());
    REQUIRE_FALSE(second->isExpanded());

    REQUIRE(window->handleMouseEvent(leftMouseDownAt(40, 155)));
    REQUIRE(window->handleMouseEvent(leftMouseUpAt(40, 155)));
    REQUIRE_FALSE(first->isExpanded());
    REQUIRE(second->isExpanded());

    REQUIRE(window->handleMouseEvent(leftMouseDownAt(260, 220)));
    REQUIRE(window->handleMouseEvent(leftMouseUpAt(260, 220)));
    REQUIRE_FALSE(first->isExpanded());
    REQUIRE_FALSE(second->isExpanded());
}

TEST_CASE("Dropdown integration selection does not consume the next owner-window click",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::test::StateWithTestPaths state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "dropdown-selection-handoff", 360, 240, SDL_WINDOW_HIDDEN);
    state.windows.push_back(window.get());

    auto root =
        std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    root->setBounds(0, 0, 360, 240);
    auto *dropdown = root->emplaceChild<cupuacu::gui::DropdownMenu>(&state);
    auto *target = root->emplaceChild<TestComponent>(&state, "Target");
    dropdown->setBounds(20, 20, 180, 30);
    dropdown->setItems({"Alpha", "Beta", "Gamma"});
    dropdown->setCollapsedHeight(30);
    target->setBounds(220, 20, 100, 40);
    window->setRootComponent(std::move(root));

    REQUIRE(window->handleMouseEvent(leftMouseDownAt(40, 35)));
    REQUIRE(window->handleMouseEvent(leftMouseUpAt(40, 35)));
    REQUIRE(dropdown->isExpanded());

    cupuacu::gui::Window *popupWindow = nullptr;
    for (auto *candidate : state.windows)
    {
        if (candidate != window.get())
        {
            popupWindow = candidate;
            break;
        }
    }
    REQUIRE(popupWindow != nullptr);
    REQUIRE(popupWindow->getRootComponent() != nullptr);

    const int popupSelectX = 40;
    const int popupSelectY = 45; // second row ("Beta")
    REQUIRE(popupWindow->handleMouseEvent(
        leftMouseDownAt(popupSelectX, popupSelectY)));
    REQUIRE(popupWindow->handleMouseEvent(
        leftMouseUpAt(popupSelectX, popupSelectY)));

    REQUIRE_FALSE(dropdown->isExpanded());
    REQUIRE(dropdown->getSelectedIndex() == 1);

    REQUIRE(window->handleMouseEvent(leftMouseDownAt(250, 35)));
    REQUIRE(window->handleMouseEvent(leftMouseUpAt(250, 35)));
    REQUIRE(target->mouseDownCount == 1);
    REQUIRE(target->mouseUpCount == 1);
}

TEST_CASE("Dropdown integration outside click closes the popup without activating underlying controls",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::test::StateWithTestPaths state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "dropdown-outside-close", 360, 240, SDL_WINDOW_HIDDEN);
    state.windows.push_back(window.get());

    auto root =
        std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    root->setBounds(0, 0, 360, 240);
    auto *dropdown = root->emplaceChild<cupuacu::gui::DropdownMenu>(&state);
    auto *target = root->emplaceChild<TestComponent>(&state, "Target");
    dropdown->setBounds(20, 20, 180, 30);
    dropdown->setItems({"Alpha", "Beta", "Gamma"});
    dropdown->setCollapsedHeight(30);
    dropdown->setSelectedIndex(1);
    target->setBounds(220, 20, 100, 40);
    window->setRootComponent(std::move(root));

    REQUIRE(window->handleMouseEvent(leftMouseDownAt(40, 35)));
    REQUIRE(window->handleMouseEvent(leftMouseUpAt(40, 35)));
    REQUIRE(dropdown->isExpanded());
    REQUIRE(dropdown->getSelectedIndex() == 1);

    cupuacu::gui::Window *popupWindow = nullptr;
    for (auto *candidate : state.windows)
    {
        if (candidate != window.get())
        {
            popupWindow = candidate;
            break;
        }
    }
    REQUIRE(popupWindow != nullptr);
    REQUIRE(popupWindow->isOpen());

    REQUIRE(window->handleMouseEvent(leftMouseDownAt(250, 35)));
    REQUIRE(window->handleMouseEvent(leftMouseUpAt(250, 35)));
    REQUIRE_FALSE(dropdown->isExpanded());
    REQUIRE(dropdown->getSelectedIndex() == 1);
    REQUIRE(state.windows.size() == 1);
    REQUIRE(state.windows[0] == window.get());
    REQUIRE(target->mouseDownCount == 0);
    REQUIRE(target->mouseUpCount == 0);

    REQUIRE(window->handleMouseEvent(leftMouseDownAt(250, 35)));
    REQUIRE(window->handleMouseEvent(leftMouseUpAt(250, 35)));
    REQUIRE(target->mouseDownCount == 1);
    REQUIRE(target->mouseUpCount == 1);
}

TEST_CASE("Main window integration opens Export Audio on primary-modifier Shift-S",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::test::StateWithTestPaths state{};
    auto sessionUi =
        cupuacu::test::integration::createSessionUi(&state, 32, false, 2);
    (void)sessionUi;

    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID =
        state.mainDocumentSessionWindow->getWindow()->getId();
    event.key.scancode = SDL_SCANCODE_S;
#if __APPLE__
    event.key.mod = SDL_KMOD_GUI | SDL_KMOD_SHIFT;
#else
    event.key.mod = SDL_KMOD_CTRL | SDL_KMOD_SHIFT;
#endif

    cupuacu::gui::handleKeyDown(&event, &state);
    REQUIRE(state.exportAudioDialogWindow != nullptr);
    REQUIRE(state.exportAudioDialogWindow->isOpen());
}

TEST_CASE("File menu integration opens a recent file into the active session",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-recent-menu"));
    const auto wavPath = cleanup.path() / "recent.wav";
    writeTestWav(wavPath, 22050, 2, {0.25f, -0.25f, 0.5f, -0.5f});

    cupuacu::test::StateWithTestPaths state{};
    auto sessionUi =
        cupuacu::test::integration::createSessionUi(&state, 16, false, 1);
    state.recentFiles = {wavPath.string()};

    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "file-recent-open", 480, 240, SDL_WINDOW_HIDDEN);

    auto root =
        std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = cupuacu::test::integration::menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 6);
    auto *fileMenu = topLevelMenus[0];
    auto fileEntries = cupuacu::test::integration::menuChildren(fileMenu);
    REQUIRE(fileEntries.size() == 9);
    auto *recentMenu = fileEntries[4];

    auto recentEntries = cupuacu::test::integration::menuChildren(recentMenu);
    REQUIRE(recentEntries.size() ==
            cupuacu::persistence::RecentFilesPersistence::kMaxEntries + 1);
    auto *recentFileEntry = recentEntries[1];

    REQUIRE(fileMenu->mouseDown(cupuacu::test::integration::leftMouseDown()));
    REQUIRE(recentMenu->mouseDown(cupuacu::test::integration::leftMouseDown()));
    REQUIRE(recentFileEntry->mouseDown(
        cupuacu::test::integration::leftMouseDown()));

    REQUIRE(state.getActiveDocumentSession().currentFile == wavPath.string());
    REQUIRE(state.getActiveDocumentSession().document.getSampleRate() == 22050);
    REQUIRE(state.getActiveDocumentSession().document.getChannelCount() == 2);
    REQUIRE(state.getActiveDocumentSession().document.getFrameCount() == 2);
    REQUIRE(state.recentFiles == std::vector<std::string>{wavPath.string()});
}

TEST_CASE("Recent submenu integration does not show blank placeholder rows",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-recent-visible"));
    const auto wavPath = cleanup.path() / "recent.wav";
    writeTestWav(wavPath, 22050, 1, {0.25f, 0.5f});

    cupuacu::test::StateWithTestPaths state{};
    state.recentFiles = {wavPath.string()};

    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "recent-visible", 480, 240, SDL_WINDOW_HIDDEN);

    auto root =
        std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = cupuacu::test::integration::menuChildren(menuBar);
    auto *fileMenu = topLevelMenus[0];
    auto *recentMenu = cupuacu::test::integration::menuChildren(fileMenu)[4];
    auto recentEntries = cupuacu::test::integration::menuChildren(recentMenu);

    REQUIRE(fileMenu->mouseDown(cupuacu::test::integration::leftMouseDown()));
    REQUIRE(recentMenu->mouseDown(cupuacu::test::integration::leftMouseDown()));

    REQUIRE_FALSE(recentEntries[0]->isVisible());
    REQUIRE(recentEntries[1]->isVisible());
    for (std::size_t i = 2; i < recentEntries.size(); ++i)
    {
        REQUIRE_FALSE(recentEntries[i]->isVisible());
    }
}

TEST_CASE("Recent submenu integration hover keeps File menu open",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-recent-hover-open"));
    const auto wavPath = cleanup.path() / "recent.wav";
    writeTestWav(wavPath, 22050, 1, {0.25f, 0.5f});

    cupuacu::test::StateWithTestPaths state{};
    state.recentFiles = {wavPath.string()};

    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "recent-hover", 480, 240, SDL_WINDOW_HIDDEN);

    auto root =
        std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = cupuacu::test::integration::menuChildren(menuBar);
    auto *fileMenu = topLevelMenus[0];
    auto *recentMenu = cupuacu::test::integration::menuChildren(fileMenu)[4];

    REQUIRE(fileMenu->mouseDown(cupuacu::test::integration::leftMouseDown()));
    REQUIRE(fileMenu->isOpen());
    REQUIRE_FALSE(recentMenu->isOpen());

    recentMenu->mouseEnter();

    REQUIRE(fileMenu->isOpen());
    REQUIRE(recentMenu->isOpen());
}

TEST_CASE("Recent submenu integration overlays file menu with slight horizontal overlap",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-recent-overlay"));
    const auto wavPath = cleanup.path() / "recent.wav";
    writeTestWav(wavPath, 22050, 1, {0.25f, 0.5f});

    cupuacu::test::StateWithTestPaths state{};
    state.recentFiles = {wavPath.string()};

    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "recent-overlay", 480, 240, SDL_WINDOW_HIDDEN);

    auto root =
        std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = cupuacu::test::integration::menuChildren(menuBar);
    auto *fileMenu = topLevelMenus[0];
    auto *recentMenu = cupuacu::test::integration::menuChildren(fileMenu)[4];
    auto *recentEntry =
        cupuacu::test::integration::menuChildren(recentMenu)[1];

    REQUIRE(fileMenu->mouseDown(cupuacu::test::integration::leftMouseDown()));
    recentMenu->mouseEnter();

    REQUIRE(recentMenu->isOpen());
    REQUIRE(recentEntry->isVisible());
    REQUIRE(recentEntry->getYPos() == 0);
    REQUIRE(recentEntry->getXPos() ==
            recentMenu->getWidth() -
                cupuacu::gui::scaleUi(&state, 10.0f));
}

TEST_CASE("Recent submenu integration closes when hovering another active top-level menu",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-recent-hover-other"));
    const auto wavPath = cleanup.path() / "recent.wav";
    writeTestWav(wavPath, 22050, 1, {0.25f, 0.5f});

    cupuacu::test::StateWithTestPaths state{};
    state.recentFiles = {wavPath.string()};

    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "recent-hover-other", 480, 240, SDL_WINDOW_HIDDEN);

    auto root =
        std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = cupuacu::test::integration::menuChildren(menuBar);
    auto *fileMenu = topLevelMenus[0];
    auto *editMenu = topLevelMenus[1];
    auto *recentMenu = cupuacu::test::integration::menuChildren(fileMenu)[4];
    auto *recentEntry =
        cupuacu::test::integration::menuChildren(recentMenu)[1];

    REQUIRE(fileMenu->mouseDown(cupuacu::test::integration::leftMouseDown()));
    recentMenu->mouseEnter();
    REQUIRE(fileMenu->isOpen());
    REQUIRE(recentMenu->isOpen());
    REQUIRE(recentEntry->isVisible());

    menuBar->setOpenSubMenuOnMouseOver(true);
    editMenu->mouseEnter();

    REQUIRE_FALSE(fileMenu->isOpen());
    REQUIRE_FALSE(recentMenu->isOpen());
    REQUIRE_FALSE(recentEntry->isVisible());
    REQUIRE(editMenu->isOpen());
}

TEST_CASE("File menu integration exit entry pushes a quit event", "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::test::StateWithTestPaths state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "file-exit", 480, 240, SDL_WINDOW_HIDDEN);

    auto root =
        std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    SDL_FlushEvents(SDL_EVENT_QUIT, SDL_EVENT_QUIT);

    auto topLevelMenus = cupuacu::test::integration::menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 6);
    auto *fileMenu = topLevelMenus[0];
    auto fileEntries = cupuacu::test::integration::menuChildren(fileMenu);
    REQUIRE(fileEntries.size() == 9);
    auto *exitEntry = fileEntries[8];

    REQUIRE(fileMenu->mouseDown(cupuacu::test::integration::leftMouseDown()));
    REQUIRE(exitEntry->mouseDown(cupuacu::test::integration::leftMouseDown()));

    SDL_Event event{};
    const int eventCount = SDL_PeepEvents(&event, 1, SDL_GETEVENT,
                                          SDL_EVENT_QUIT, SDL_EVENT_QUIT);
    REQUIRE(eventCount == 1);
    REQUIRE(event.type == SDL_EVENT_QUIT);
}
