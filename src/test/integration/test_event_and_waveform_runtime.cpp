#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "IntegrationTestHelpers.hpp"
#include "../TestSdlLogSilencer.hpp"
#include "../TestPaths.hpp"

#include "State.hpp"
#include "actions/DocumentLifecycle.hpp"
#include "actions/ZoomPlanning.hpp"
#include "file/SndfilePath.hpp"
#include "gui/DropdownMenu.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/DocumentMarkerHandle.hpp"
#include "gui/EventHandling.hpp"
#include "gui/GenerateSilenceDialogWindow.hpp"
#include "gui/Gui.hpp"
#include "gui/LabeledField.hpp"
#include "gui/MarkerEditorDialogWindow.hpp"
#include "gui/NewFileDialogWindow.hpp"
#include "gui/SamplePoint.hpp"
#include "gui/StatusBar.hpp"
#include "gui/TabStrip.hpp"
#include "gui/TextButton.hpp"
#include "gui/TextInput.hpp"
#include "gui/TriangleMarker.hpp"
#include "gui/Waveform.hpp"
#include "gui/WaveformsUnderlay.hpp"
#include "gui/Window.hpp"

#include <SDL3/SDL.h>
#include <sndfile.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <random>
#include <system_error>
#include <vector>

namespace
{
    class KeyAwareComponent : public cupuacu::gui::Component
    {
    public:
        explicit KeyAwareComponent(cupuacu::State *state)
            : Component(state, "KeyAwareComponent")
        {
        }

        int keyDownCount = 0;
        int textInputCount = 0;

        bool acceptsKeyboardFocus() const override
        {
            return true;
        }

        bool keyDown(const SDL_KeyboardEvent &) override
        {
            ++keyDownCount;
            return true;
        }

        bool textInput(const std::string_view) override
        {
            ++textInputCount;
            return true;
        }
    };

    struct BuiltSessionUi
    {
        std::unique_ptr<cupuacu::gui::Window> auxiliaryWindow;
    };

    BuiltSessionUi createBuiltSessionUi(cupuacu::State *state,
                                        const int64_t frameCount,
                                        const int sampleRate = 44100,
                                        const int channels = 2,
                                        const int width = 800,
                                        const int height = 400)
    {
        cupuacu::test::ensureSdlTtfInitialized();
        cupuacu::test::ensureTestPaths(*state, "event-and-waveform-runtime");

        auto &session = state->getActiveDocumentSession();
        session.document.initialize(cupuacu::SampleFormat::FLOAT32, sampleRate,
                                    channels, frameCount);
        session.syncSelectionAndCursorToDocumentLength();

        state->mainDocumentSessionWindow =
            std::make_unique<cupuacu::gui::DocumentSessionWindow>(
                state, &session, &state->getActiveViewState(), "built-session", width, height,
                SDL_WINDOW_HIDDEN);
        cupuacu::gui::buildComponents(state,
                                      state->mainDocumentSessionWindow->getWindow());

        return {};
    }

    BuiltSessionUi createBuiltEmptySessionUi(cupuacu::State *state,
                                             const int width = 800,
                                             const int height = 400)
    {
        cupuacu::test::ensureSdlTtfInitialized();
        cupuacu::test::ensureTestPaths(*state, "event-and-waveform-runtime");

        auto &session = state->getActiveDocumentSession();
        session.clearCurrentFile();
        session.document.initialize(cupuacu::SampleFormat::Unknown, 0, 0, 0);
        session.selection.reset();
        session.cursor = 0;
        session.syncSelectionAndCursorToDocumentLength();

        state->mainDocumentSessionWindow =
            std::make_unique<cupuacu::gui::DocumentSessionWindow>(
                state, &session, &state->getActiveViewState(), "built-session",
                width, height, SDL_WINDOW_HIDDEN);
        cupuacu::gui::buildComponents(state,
                                      state->mainDocumentSessionWindow->getWindow());

        return {};
    }

    cupuacu::gui::SamplePoint *firstSamplePoint(cupuacu::gui::Waveform *waveform)
    {
        for (const auto &child : waveform->getChildren())
        {
            if (auto *point =
                    dynamic_cast<cupuacu::gui::SamplePoint *>(child.get()))
            {
                return point;
            }
        }

        return nullptr;
    }

    cupuacu::gui::LabeledField *findStatusField(cupuacu::gui::Component *root,
                                                const std::string_view label)
    {
        return cupuacu::test::integration::findByNameRecursive<
            cupuacu::gui::LabeledField>(
            root, std::string("LabeledField for ") + std::string(label));
    }

    template <typename T>
    T *findFirstRecursive(cupuacu::gui::Component *root)
    {
        if (!root)
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

    template <typename T>
    void collectRecursive(cupuacu::gui::Component *root, std::vector<T *> &out)
    {
        if (!root)
        {
            return;
        }
        if (auto *typed = dynamic_cast<T *>(root))
        {
            out.push_back(typed);
        }
        for (const auto &child : root->getChildren())
        {
            collectRecursive(child.get(), out);
        }
    }

    void clickButton(cupuacu::gui::TextButton *button)
    {
        REQUIRE(button != nullptr);
        REQUIRE(button->mouseDown(cupuacu::test::integration::leftMouseDown()));
        REQUIRE(button->mouseUp(cupuacu::test::integration::leftMouseUp()));
    }

    void clickTabClose(cupuacu::gui::TabStripTab *tab)
    {
        REQUIRE(tab != nullptr);

        const int x = tab->getWidth() - 8;
        const int y = tab->getHeight() / 2;
        const auto down = cupuacu::gui::MouseEvent{
            cupuacu::gui::DOWN, x, y, static_cast<float>(x),
            static_cast<float>(y), static_cast<float>(x),
            static_cast<float>(y),
            cupuacu::gui::MouseButtonState{true, false, false}, 1};
        const auto up = cupuacu::gui::MouseEvent{
            cupuacu::gui::UP, x, y, static_cast<float>(x),
            static_cast<float>(y), static_cast<float>(x),
            static_cast<float>(y),
            cupuacu::gui::MouseButtonState{true, false, false}, 1};

        REQUIRE(tab->mouseDown(down));
        REQUIRE(tab->mouseUp(up));
    }

    void processPendingSdlWindowEvents(cupuacu::State *state)
    {
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            cupuacu::gui::handleAppEvent(state, &event);
        }
    }

    cupuacu::gui::MouseEvent componentMouseEvent(
        cupuacu::gui::Component *component, const cupuacu::gui::MouseEventType type,
        const uint8_t clicks = 1, const bool leftDown = true)
    {
        REQUIRE(component != nullptr);
        const auto bounds = component->getAbsoluteBounds();
        const int x = bounds.x + std::max(1, bounds.w / 2);
        const int y = bounds.y + std::max(1, bounds.h / 2);
        return cupuacu::gui::MouseEvent{
            type,
            x,
            y,
            static_cast<float>(x),
            static_cast<float>(y),
            0.0f,
            0.0f,
            cupuacu::gui::MouseButtonState{leftDown, false, false},
            clicks};
    }

    void clickComponentThroughWindow(cupuacu::gui::Window *window,
                                     cupuacu::gui::Component *component,
                                     const uint8_t clicks = 1)
    {
        REQUIRE(window != nullptr);
        REQUIRE(component != nullptr);
        REQUIRE(window->handleMouseEvent(
            componentMouseEvent(component, cupuacu::gui::DOWN, clicks, true)));
        REQUIRE(window->handleMouseEvent(
            componentMouseEvent(component, cupuacu::gui::UP, clicks, true)));
    }

    void moveMouseOverComponent(cupuacu::gui::Window *window,
                                cupuacu::gui::Component *component)
    {
        REQUIRE(window != nullptr);
        REQUIRE(component != nullptr);
        REQUIRE(window->handleMouseEvent(
            componentMouseEvent(component, cupuacu::gui::MOVE, 0, false)));
    }

    cupuacu::gui::DocumentMarkerHandle *findTopMarkerHandle(
        cupuacu::gui::Component *root, const uint64_t markerId)
    {
        return cupuacu::test::integration::findByNameRecursive<
            cupuacu::gui::DocumentMarkerHandle>(
            root, "DocumentMarkerHandle:Top:" + std::to_string(markerId));
    }

    std::vector<cupuacu::gui::TextInput *> findTextInputs(
        cupuacu::gui::Component *root)
    {
        std::vector<cupuacu::gui::TextInput *> result;
        collectRecursive(root, result);
        return result;
    }

    void sendKeyDown(cupuacu::gui::Window *window, const SDL_Scancode scancode)
    {
        REQUIRE(window != nullptr);

        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.windowID = window->getId();
        event.key.scancode = scancode;
        window->handleEvent(event);
    }

    cupuacu::gui::Window *openMarkerEditorOrSkip(cupuacu::State &state,
                                                 cupuacu::gui::Window *mainWindow,
                                                 cupuacu::gui::Component *handle)
    {
        REQUIRE(mainWindow != nullptr);
        REQUIRE(handle != nullptr);

        clickComponentThroughWindow(mainWindow, handle, 2);
        if (state.markerEditorDialogWindow == nullptr || state.modalWindow == nullptr)
        {
            SKIP("Marker editor dialog window unavailable in this SDL environment");
        }

        REQUIRE(state.markerEditorDialogWindow->isOpen());
        return state.modalWindow;
    }

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
} // namespace

TEST_CASE("Keyboard integration zooms to selection and scrolls horizontally",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 4096);

    auto &viewState = state.getActiveViewState();
    viewState.samplesPerPixel = 8.0;
    viewState.sampleOffset = 160;
    state.getActiveDocumentSession().selection.setValue1(200);
    state.getActiveDocumentSession().selection.setValue2(500);

    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_Z;
    cupuacu::gui::handleKeyDown(&event, &state);

    REQUIRE(viewState.sampleOffset == 200);
    REQUIRE(viewState.samplesPerPixel == Catch::Approx(
                                         300.0 / cupuacu::gui::Waveform::getWaveformWidth(
                                                     &state)));

    const auto offsetAfterZoomSelection = viewState.sampleOffset;

    event.key.scancode = SDL_SCANCODE_RIGHT;
    cupuacu::gui::handleKeyDown(&event, &state);
    REQUIRE(viewState.sampleOffset > offsetAfterZoomSelection);

    const auto offsetAfterRight = viewState.sampleOffset;
    event.key.scancode = SDL_SCANCODE_LEFT;
    cupuacu::gui::handleKeyDown(&event, &state);
    REQUIRE(viewState.sampleOffset < offsetAfterRight);
}

TEST_CASE("Keyboard integration applies zoom and pixel scale shortcuts",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 4096);

    auto &viewState = state.getActiveViewState();
    viewState.samplesPerPixel = 4.0;
    viewState.sampleOffset = 64;

    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_W;
    cupuacu::gui::handleKeyDown(&event, &state);
    REQUIRE(viewState.samplesPerPixel < 4.0);

    const auto zoomedInSamplesPerPixel = viewState.samplesPerPixel;
    event.key.scancode = SDL_SCANCODE_Q;
    cupuacu::gui::handleKeyDown(&event, &state);
    REQUIRE(viewState.samplesPerPixel > zoomedInSamplesPerPixel);

    const auto samplesPerPixelBeforeScale = viewState.samplesPerPixel;
    event.key.scancode = SDL_SCANCODE_PERIOD;
    event.key.mod = SDL_KMOD_SHIFT;
    cupuacu::gui::handleKeyDown(&event, &state);
    REQUIRE(state.pixelScale == 2);
    REQUIRE(viewState.samplesPerPixel ==
            Catch::Approx(samplesPerPixelBeforeScale * 2.0));

    event.key.scancode = SDL_SCANCODE_COMMA;
    event.key.mod = SDL_KMOD_SHIFT;
    cupuacu::gui::handleKeyDown(&event, &state);
    REQUIRE(state.pixelScale == 1);
    REQUIRE(viewState.samplesPerPixel ==
            Catch::Approx(samplesPerPixelBeforeScale));
}

TEST_CASE("Keyboard integration opens new file dialog and closes the active tab",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 256, 44100, 2, 800, 400);
    state.getActiveDocumentSession().currentFile = "/tmp/current.wav";
    state.getActiveDocumentSession().selection.setValue1(10.0);
    state.getActiveDocumentSession().selection.setValue2(20.0);
    state.getActiveDocumentSession().cursor = 12;

    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.mod = SDL_KMOD_CTRL;

    event.key.scancode = SDL_SCANCODE_N;
    cupuacu::gui::handleKeyDown(&event, &state);
    REQUIRE(state.newFileDialogWindow != nullptr);
    REQUIRE(state.newFileDialogWindow->isOpen());

    event.key.scancode = SDL_SCANCODE_W;
    cupuacu::gui::handleKeyDown(&event, &state);
    REQUIRE(state.getActiveDocumentSession().currentFile.empty());
    REQUIRE(state.getActiveDocumentSession().document.getChannelCount() == 0);
    REQUIRE_FALSE(state.getActiveDocumentSession().selection.isActive());
    REQUIRE(state.getActiveDocumentSession().cursor == 0);
}

TEST_CASE("Keyboard integration closes the active tab instead of blanking it",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 256, 44100, 2, 800, 400);

    state.tabs.resize(2);
    state.tabs[0].session.currentFile = "/tmp/first.wav";
    state.tabs[0].session.document.initialize(
        cupuacu::SampleFormat::PCM_S16, 44100, 2, 256);
    state.tabs[1].session.currentFile = "/tmp/second.wav";
    state.tabs[1].session.document.initialize(
        cupuacu::SampleFormat::PCM_S16, 44100, 2, 256);
    state.activeTabIndex = 1;
    state.mainDocumentSessionWindow->bindDocumentSession(
        &state.getActiveDocumentSession(), &state.getActiveViewState());

    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.mod = SDL_KMOD_CTRL;
    event.key.scancode = SDL_SCANCODE_W;
    cupuacu::gui::handleKeyDown(&event, &state);

    REQUIRE(state.tabs.size() == 1);
    REQUIRE(state.activeTabIndex == 0);
    REQUIRE(state.getActiveDocumentSession().currentFile == "/tmp/first.wav");
}

TEST_CASE("Event handling integration blocks interactive events behind modal",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};

    auto mainWindow = std::make_unique<cupuacu::gui::Window>(
        &state, "main-window", 320, 240, 0);
    auto modalWindow = std::make_unique<cupuacu::gui::Window>(
        &state, "modal-window", 240, 160, 0);

    auto root =
        std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *hovered = root->emplaceChild<cupuacu::gui::Component>(&state,
                                                                "Hovered");
    hovered->setBounds(0, 0, 100, 100);
    root->setBounds(0, 0, 320, 240);
    mainWindow->setRootComponent(std::move(root));
    mainWindow->updateComponentUnderMouse(20, 20);
    REQUIRE(mainWindow->getComponentUnderMouse() == hovered);

    state.mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            &state, &state.getActiveDocumentSession(), &state.getActiveViewState(), "main-doc", 320, 240, 0);
    state.mainDocumentSessionWindow->getWindow()->setRootComponent(
        std::make_unique<cupuacu::test::integration::RootComponent>(&state));
    state.modalWindow = modalWindow.get();

    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = mainWindow->getId();
    event.motion.x = 200;
    event.motion.y = 200;

    REQUIRE(cupuacu::gui::handleAppEvent(&state, &event) == SDL_APP_CONTINUE);
    REQUIRE(mainWindow->getComponentUnderMouse() == hovered);
}

TEST_CASE("Event handling integration still forwards non-interactive window events behind modal",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 512);

    auto *mainWindow = state.mainDocumentSessionWindow->getWindow();
    state.windows.push_back(mainWindow);

    auto modalWindow = std::make_unique<cupuacu::gui::Window>(
        &state, "modal-window", 240, 160, SDL_WINDOW_HIDDEN);
    state.modalWindow = modalWindow.get();
    state.windows.push_back(modalWindow.get());

    int resizeCount = 0;
    mainWindow->setOnResize([&]() { ++resizeCount; });

    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_RESIZED;
    event.window.windowID = mainWindow->getId();

    REQUIRE(cupuacu::gui::handleAppEvent(&state, &event) == SDL_APP_CONTINUE);
    REQUIRE(resizeCount == 1);
}

TEST_CASE("Event handling integration returns success for quit and keeps app open on main-window close requests",
          "[integration]")
{
    SECTION("quit event exits immediately")
    {
        cupuacu::test::StateWithTestPaths state{};
        createBuiltSessionUi(&state, 256);

        SDL_Event event{};
        event.type = SDL_EVENT_QUIT;

        REQUIRE(cupuacu::gui::handleAppEvent(&state, &event) == SDL_APP_SUCCESS);
        REQUIRE(state.mainDocumentSessionWindow->getWindow()->isOpen());
    }

    SECTION("main document close request exits the app")
    {
        cupuacu::test::StateWithTestPaths state{};
        createBuiltSessionUi(&state, 256);
        state.getActiveDocumentSession().currentFile = "/tmp/current.wav";
        state.getActiveDocumentSession().selection.setValue1(10.0);
        state.getActiveDocumentSession().selection.setValue2(20.0);
        state.getActiveDocumentSession().cursor = 12;

        auto *mainWindow = state.mainDocumentSessionWindow->getWindow();
        state.windows.push_back(mainWindow);

        SDL_Event event{};
        event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        event.window.windowID = mainWindow->getId();

        REQUIRE(cupuacu::gui::handleAppEvent(&state, &event) == SDL_APP_SUCCESS);
        REQUIRE(mainWindow->isOpen());
        REQUIRE(state.getActiveDocumentSession().currentFile == "/tmp/current.wav");
        REQUIRE(state.getActiveDocumentSession().document.getChannelCount() == 2);
        REQUIRE(state.getActiveDocumentSession().selection.isActive());
        REQUIRE(state.getActiveDocumentSession().cursor == 12);
    }
}

TEST_CASE("Startup document restore integration reopens the most recent file",
          "[integration]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-startup-restore"));
    const auto wavPath = cleanup.path() / "startup.wav";
    writeTestWav(wavPath, 48000, 2, {0.1f, -0.1f, 0.2f, -0.2f, 0.3f, -0.3f});

    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 8);
    cupuacu::persistence::PersistedSessionState persistedState{};
    persistedState.openFiles = {wavPath.string()};
    persistedState.activeOpenFileIndex = 0;

    state.getActiveDocumentSession().currentFile = "before.wav";
    state.getActiveDocumentSession().selection.setValue1(1.0);
    state.getActiveDocumentSession().selection.setValue2(3.0);
    state.getActiveDocumentSession().cursor = 2;

    cupuacu::actions::restoreStartupDocument(
        &state, {wavPath.string()}, persistedState);

    REQUIRE(state.getActiveDocumentSession().currentFile == wavPath.string());
    REQUIRE(state.getActiveDocumentSession().document.getSampleRate() == 48000);
    REQUIRE(state.getActiveDocumentSession().document.getChannelCount() == 2);
    REQUIRE(state.getActiveDocumentSession().document.getFrameCount() == 3);
    REQUIRE_FALSE(state.getActiveDocumentSession().selection.isActive());
    REQUIRE(state.getActiveDocumentSession().cursor == 0);
    REQUIRE(state.recentFiles == std::vector<std::string>{wavPath.string()});
}

TEST_CASE("Startup document restore integration reopens multiple file-backed tabs",
          "[integration]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-startup-restore-multi"));
    const auto firstPath = cleanup.path() / "first.wav";
    const auto secondPath = cleanup.path() / "second.wav";
    writeTestWav(firstPath, 44100, 1, {0.1f, 0.2f, 0.3f});
    writeTestWav(secondPath, 22050, 2, {0.4f, -0.4f, 0.5f, -0.5f});

    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 8);

    cupuacu::persistence::PersistedSessionState persistedState{};
    persistedState.openFiles = {firstPath.string(), secondPath.string()};
    persistedState.activeOpenFileIndex = 1;

    cupuacu::actions::restoreStartupDocument(
        &state, {secondPath.string(), firstPath.string()}, persistedState);

    REQUIRE(state.tabs.size() == 2);
    REQUIRE(state.activeTabIndex == 1);
    REQUIRE(state.tabs[0].session.currentFile == firstPath.string());
    REQUIRE(state.tabs[0].session.document.getSampleRate() == 44100);
    REQUIRE(state.tabs[1].session.currentFile == secondPath.string());
    REQUIRE(state.tabs[1].session.document.getSampleRate() == 22050);
    REQUIRE(state.getActiveDocumentSession().currentFile == secondPath.string());
}

TEST_CASE("Startup document restore integration skips unreadable existing paths",
          "[integration]")
{
    cupuacu::test::ScopedSdlLogSilencer silenceLogs;
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-startup-restore-unreadable"));
    const auto validPath = cleanup.path() / "valid.wav";
    const auto unreadablePath = cleanup.path() / "not-a-file";
    writeTestWav(validPath, 44100, 1, {0.1f, 0.2f, 0.3f});
    std::filesystem::create_directories(unreadablePath);

    cupuacu::test::StateWithTestPaths state{};
    createBuiltEmptySessionUi(&state);

    cupuacu::persistence::PersistedSessionState persistedState{};
    persistedState.openFiles = {unreadablePath.string(), validPath.string()};
    persistedState.activeOpenFileIndex = 1;

    std::string reportedMessage;
    state.errorReporter =
        [&](const std::string &, const std::string &message)
    {
        reportedMessage = message;
    };

    cupuacu::actions::restoreStartupDocument(
        &state, {unreadablePath.string(), validPath.string()}, persistedState);

    REQUIRE(state.tabs.size() == 1);
    REQUIRE(state.getActiveDocumentSession().currentFile == validPath.string());
    REQUIRE(state.recentFiles == std::vector<std::string>{validPath.string()});
    REQUIRE(reportedMessage.find(unreadablePath.string()) != std::string::npos);
}

TEST_CASE("Event handling integration ignores unfocused key and text input",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::test::StateWithTestPaths state{};
    state.mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            &state, &state.getActiveDocumentSession(), &state.getActiveViewState(), "main-doc", 320, 240,
            SDL_WINDOW_HIDDEN);

    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "unfocused-input-window", 320, 240, SDL_WINDOW_HIDDEN);
    state.windows.push_back(window.get());

    auto root =
        std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *focused = root->emplaceChild<KeyAwareComponent>(&state);
    focused->setBounds(0, 0, 100, 50);
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

    REQUIRE(cupuacu::gui::handleAppEvent(&state, &keyEvent) ==
            SDL_APP_CONTINUE);
    REQUIRE(cupuacu::gui::handleAppEvent(&state, &textEvent) ==
            SDL_APP_CONTINUE);
    REQUIRE(focused->keyDownCount == 0);
    REQUIRE(focused->textInputCount == 0);
}

TEST_CASE("Event handling integration clears waveform highlight on main window mouse leave",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 1024);

    auto *mainWindow = state.mainDocumentSessionWindow->getWindow();
    REQUIRE_FALSE(state.waveforms.empty());
    auto *waveform = state.waveforms.front();

    waveform->setSamplePosUnderCursor(42);
    mainWindow->setComponentUnderMouse(waveform);
    state.getActiveDocumentSession().selection.reset();

    cupuacu::gui::handleWindowMouseLeave(&state, mainWindow);
    REQUIRE_FALSE(waveform->getSamplePosUnderCursor().has_value());
    REQUIRE(mainWindow->getComponentUnderMouse() == nullptr);
}

TEST_CASE("Window integration routes focused key input to focused component",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::test::StateWithTestPaths state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "focused-input-window", 320, 240, SDL_WINDOW_HIDDEN);

    auto root =
        std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *focused = root->emplaceChild<KeyAwareComponent>(&state);
    focused->setBounds(0, 0, 100, 50);
    root->setBounds(0, 0, 320, 240);
    window->setRootComponent(std::move(root));
    window->setFocusedComponent(focused);

    SDL_Event keyEvent{};
    keyEvent.type = SDL_EVENT_KEY_DOWN;
    keyEvent.key.windowID = window->getId();
    keyEvent.key.scancode = SDL_SCANCODE_A;

    REQUIRE(window->handleEvent(keyEvent));
    REQUIRE(focused->keyDownCount == 1);
    REQUIRE(focused->textInputCount == 0);
}

TEST_CASE("Status bar integration lays out labeled fields across the footer",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 1024, 44100, 2, 800, 400);

    auto *mainWindow = state.mainDocumentSessionWindow->getWindow();
    mainWindow->renderFrame();

    auto *statusBar = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::StatusBar>(mainWindow->getContentLayer(), "StatusBar");
    REQUIRE(statusBar != nullptr);

    auto *posField = findStatusField(statusBar, "Pos");
    auto *startField = findStatusField(statusBar, "St");
    auto *endField = findStatusField(statusBar, "End");
    auto *lengthField = findStatusField(statusBar, "Len");
    auto *valueField = findStatusField(statusBar, "Val");
    auto *rateField = findStatusField(statusBar, "Rate");
    auto *depthField = findStatusField(statusBar, "Depth");

    REQUIRE(posField != nullptr);
    REQUIRE(startField != nullptr);
    REQUIRE(endField != nullptr);
    REQUIRE(lengthField != nullptr);
    REQUIRE(valueField != nullptr);
    REQUIRE(rateField != nullptr);
    REQUIRE(depthField != nullptr);

    const auto initialPosBounds = posField->getBounds();
    const auto initialStartBounds = startField->getBounds();
    const auto initialEndBounds = endField->getBounds();
    const auto initialLengthBounds = lengthField->getBounds();
    const auto initialValueBounds = valueField->getBounds();

    REQUIRE(initialStartBounds.x > initialPosBounds.x);
    REQUIRE(initialEndBounds.x > initialStartBounds.x);
    REQUIRE(initialLengthBounds.x > initialEndBounds.x);
    REQUIRE(initialValueBounds.x > initialLengthBounds.x);
    REQUIRE(rateField->getBounds().x > initialValueBounds.x);
    REQUIRE(depthField->getBounds().x > rateField->getBounds().x);
    REQUIRE(initialPosBounds.w == initialStartBounds.w);
    REQUIRE(initialStartBounds.w == initialEndBounds.w);
}

TEST_CASE("Status bar integration tolerates missing audio devices",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 1024, 44100, 2, 800, 400);

    auto *statusBar = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::StatusBar>(
        state.mainDocumentSessionWindow->getWindow()->getContentLayer(),
        "StatusBar");
    REQUIRE(statusBar != nullptr);

    auto *posField = findStatusField(statusBar, "Pos");
    auto *startField = findStatusField(statusBar, "St");
    REQUIRE(posField != nullptr);
    REQUIRE(startField != nullptr);

    state.audioDevices.reset();
    state.getActiveDocumentSession().cursor = 321;

    statusBar->timerCallback();

    REQUIRE(posField->isDirty());
    REQUIRE(startField->isDirty());
}

TEST_CASE("Status bar integration inline position edit submits on enter and cancels on escape",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 1024, 44100, 2, 800, 400);

    auto *mainWindow = state.mainDocumentSessionWindow->getWindow();
    auto *statusBar = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::StatusBar>(mainWindow->getContentLayer(), "StatusBar");
    REQUIRE(statusBar != nullptr);

    auto *posField = findStatusField(statusBar, "Pos");
    REQUIRE(posField != nullptr);

    statusBar->timerCallback();
    REQUIRE(posField->getValue() == "0");

    REQUIRE(posField->mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN, 10, 10, 10.0f, 10.0f, 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{true, false, false}, 2}));
    REQUIRE(posField->isEditing());

    auto *editor = findFirstRecursive<cupuacu::gui::TextInput>(posField);
    REQUIRE(editor != nullptr);
    REQUIRE(editor->textInput("12"));

    state.getActiveDocumentSession().cursor = 77;
    statusBar->timerCallback();
    REQUIRE(editor->getText() == "12");

    SDL_KeyboardEvent enter{};
    enter.scancode = SDL_SCANCODE_RETURN;
    REQUIRE(editor->keyDown(enter));
    statusBar->timerCallback();
    REQUIRE_FALSE(posField->isEditing());
    REQUIRE(state.getActiveDocumentSession().cursor == 12);
    REQUIRE(posField->getValue() == "12");

    REQUIRE(posField->mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN, 10, 10, 10.0f, 10.0f, 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{true, false, false}, 2}));
    REQUIRE(posField->isEditing());
    editor = findFirstRecursive<cupuacu::gui::TextInput>(posField);
    REQUIRE(editor != nullptr);
    REQUIRE(editor->textInput("34"));

    SDL_KeyboardEvent escape{};
    escape.scancode = SDL_SCANCODE_ESCAPE;
    REQUIRE(editor->keyDown(escape));
    statusBar->timerCallback();
    REQUIRE_FALSE(posField->isEditing());
    REQUIRE(state.getActiveDocumentSession().cursor == 12);
    REQUIRE(posField->getValue() == "12");
}

TEST_CASE("Status bar integration inline length edit creates a selection from the cursor",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 1024, 44100, 2, 800, 400);

    auto *mainWindow = state.mainDocumentSessionWindow->getWindow();
    auto *statusBar = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::StatusBar>(mainWindow->getContentLayer(), "StatusBar");
    REQUIRE(statusBar != nullptr);

    auto *lengthField = findStatusField(statusBar, "Len");
    auto *startField = findStatusField(statusBar, "St");
    auto *endField = findStatusField(statusBar, "End");
    REQUIRE(lengthField != nullptr);
    REQUIRE(startField != nullptr);
    REQUIRE(endField != nullptr);

    state.getActiveDocumentSession().cursor = 100;
    statusBar->timerCallback();

    REQUIRE(lengthField->mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN, 10, 10, 10.0f, 10.0f, 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{true, false, false}, 2}));

    auto *editor = findFirstRecursive<cupuacu::gui::TextInput>(lengthField);
    REQUIRE(editor != nullptr);
    REQUIRE(editor->textInput("25"));

    SDL_KeyboardEvent enter{};
    enter.scancode = SDL_SCANCODE_RETURN;
    REQUIRE(editor->keyDown(enter));
    statusBar->timerCallback();

    const auto &selection = state.getActiveDocumentSession().selection;
    REQUIRE(selection.isActive());
    REQUIRE(selection.getStartInt() == 100);
    REQUIRE(selection.getEndInt() == 124);
    REQUIRE(selection.getLengthInt() == 25);
    REQUIRE(startField->getValue() == "100");
    REQUIRE(endField->getValue() == "124");
    REQUIRE(lengthField->getValue() == "25");
}

TEST_CASE("Status bar integration shows sample rate and bit depth",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 32, 48000, 2, 800, 400);

    auto *statusBar = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::StatusBar>(
        state.mainDocumentSessionWindow->getWindow()->getContentLayer(),
        "StatusBar");
    REQUIRE(statusBar != nullptr);

    statusBar->timerCallback();

    auto *rateField = findStatusField(statusBar, "Rate");
    auto *depthField = findStatusField(statusBar, "Depth");
    REQUIRE(rateField != nullptr);
    REQUIRE(depthField != nullptr);
    REQUIRE(rateField->getValue() == "48000");
    REQUIRE(depthField->getValue() == "32-bit");
}

TEST_CASE("Startup restore integration with built main window preserves persisted view state",
          "[integration]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-startup-restore-built-view"));
    const auto validPath = cleanup.path() / "view.wav";
    std::vector<float> samples(6000, 0.0f);
    for (size_t i = 0; i < samples.size(); ++i)
    {
        samples[i] = static_cast<float>(i % 17) / 17.0f;
    }
    writeTestWav(validPath, 32000, 1, samples);

    cupuacu::test::StateWithTestPaths state{};
    createBuiltEmptySessionUi(&state, 800, 400);

    cupuacu::persistence::PersistedSessionState persistedState{};
    persistedState.openDocuments = {
        {
            .filePath = validPath.string(),
            .samplesPerPixel = 2.75,
            .sampleOffset = 200,
        },
    };
    persistedState.openFiles = {validPath.string()};
    persistedState.activeOpenFileIndex = 0;

    cupuacu::actions::restoreStartupDocument(
        &state, {validPath.string()}, persistedState);

    const auto &viewState = state.getActiveViewState();
    REQUIRE(viewState.samplesPerPixel == Catch::Approx(2.75));
    REQUIRE(viewState.sampleOffset == 200);
}

TEST_CASE("Status bar integration shows persisted integer sample values for PCM",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 32, 44100, 1, 800, 400);

    auto *statusBar = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::StatusBar>(
        state.mainDocumentSessionWindow->getWindow()->getContentLayer(),
        "StatusBar");
    REQUIRE(statusBar != nullptr);

    auto *valueField = findStatusField(statusBar, "Val");
    REQUIRE(valueField != nullptr);

    auto &session = state.getActiveDocumentSession();

    SECTION("edited PCM16 samples use the quantized writer code")
    {
        session.document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 1);
        session.document.setSample(0, 0, -1.0f);
        state.getActiveViewState().sampleValueUnderMouseCursor =
            cupuacu::gui::HoveredSampleInfo{-1.0f, 0, 0};

        statusBar->timerCallback();

        REQUIRE(valueField->getValue() == "-32,767");
    }

    SECTION("untouched loaded PCM16 samples preserve the original sample code")
    {
        session.document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 1);
        session.document.setSample(0, 0, -1.0f, false);
        state.getActiveViewState().sampleValueUnderMouseCursor =
            cupuacu::gui::HoveredSampleInfo{-1.0f, 0, 0};

        statusBar->timerCallback();

        REQUIRE(valueField->getValue() == "-32,768");
    }

    SECTION("PCM8 uses the true signed 8-bit range")
    {
        session.document.initialize(cupuacu::SampleFormat::PCM_S8, 44100, 1, 1);
        session.document.setSample(0, 0, 1.0f);
        state.getActiveViewState().sampleValueUnderMouseCursor =
            cupuacu::gui::HoveredSampleInfo{1.0f, 0, 0};

        statusBar->timerCallback();

        REQUIRE(valueField->getValue() == "127");
    }

    SECTION("PCM24 inserts separators for million-scale values")
    {
        session.document.initialize(cupuacu::SampleFormat::PCM_S24, 44100, 1, 1);
        constexpr int64_t targetCode = 1234567;
        constexpr double scale = static_cast<double>(int64_t{1} << 23);
        const float sample = static_cast<float>(targetCode / scale);
        session.document.setSample(0, 0, sample, false);
        state.getActiveViewState().sampleValueUnderMouseCursor =
            cupuacu::gui::HoveredSampleInfo{sample, 0, 0};

        statusBar->timerCallback();

        REQUIRE(valueField->getValue() == "1,234,567");
    }
}

TEST_CASE("New file dialog integration creates an empty document with the selected format",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 16, 44100, 2, 800, 400);

    state.newFileDialogWindow.reset(new cupuacu::gui::NewFileDialogWindow(&state));
    REQUIRE(state.newFileDialogWindow != nullptr);
    REQUIRE(state.newFileDialogWindow->isOpen());

    auto *root = state.newFileDialogWindow->getWindow()->getRootComponent();
    std::vector<cupuacu::gui::DropdownMenu *> dropdowns;
    collectRecursive(root, dropdowns);
    REQUIRE(dropdowns.size() >= 3);
    dropdowns[0]->setSelectedIndex(4);
    dropdowns[1]->setSelectedIndex(0);
    dropdowns[2]->setSelectedIndex(0);

    auto *okButton =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::TextButton>(
            root, "TextButton:OK");
    clickButton(okButton);

    auto &session = state.getActiveDocumentSession();
    REQUIRE(session.currentFile.empty());
    REQUIRE(session.document.getSampleRate() == 96000);
    REQUIRE(session.document.getSampleFormat() == cupuacu::SampleFormat::PCM_S8);
    REQUIRE(session.document.getChannelCount() == 1);
    REQUIRE(session.document.getFrameCount() == 0);
}

TEST_CASE("New file dialog integration treats Enter as OK", "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 16, 44100, 2, 800, 400);

    state.newFileDialogWindow.reset(new cupuacu::gui::NewFileDialogWindow(&state));
    REQUIRE(state.newFileDialogWindow != nullptr);
    REQUIRE(state.newFileDialogWindow->isOpen());

    auto *root = state.newFileDialogWindow->getWindow()->getRootComponent();
    std::vector<cupuacu::gui::DropdownMenu *> dropdowns;
    collectRecursive(root, dropdowns);
    REQUIRE(dropdowns.size() >= 3);
    dropdowns[0]->setSelectedIndex(3);
    dropdowns[1]->setSelectedIndex(1);
    dropdowns[2]->setSelectedIndex(1);

    sendKeyDown(state.newFileDialogWindow->getWindow(), SDL_SCANCODE_RETURN);

    auto &session = state.getActiveDocumentSession();
    REQUIRE(session.document.getSampleRate() == 48000);
    REQUIRE(session.document.getSampleFormat() == cupuacu::SampleFormat::PCM_S16);
    REQUIRE(session.document.getChannelCount() == 2);
    REQUIRE(session.document.getFrameCount() == 0);
}

TEST_CASE("New file dialog integration can be opened and closed repeatedly",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 16, 44100, 2, 800, 400);

    for (int i = 0; i < 3; ++i)
    {
        state.newFileDialogWindow.reset(new cupuacu::gui::NewFileDialogWindow(&state));
        REQUIRE(state.newFileDialogWindow != nullptr);
        REQUIRE(state.newFileDialogWindow->isOpen());

        auto *dialogRoot =
            state.newFileDialogWindow->getWindow()->getRootComponent();
        auto *okButton = findFirstRecursive<cupuacu::gui::TextButton>(dialogRoot);
        REQUIRE(okButton != nullptr);

        clickButton(okButton);
        processPendingSdlWindowEvents(&state);
        REQUIRE_FALSE(state.newFileDialogWindow->isOpen());
        state.newFileDialogWindow.reset();
    }
}

TEST_CASE("Tab strip integration switches the active document", "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.tabs.resize(2);
    state.activeTabIndex = 0;

    state.tabs[0].session.currentFile = "/tmp/first.wav";
    state.tabs[0].session.document.initialize(cupuacu::SampleFormat::FLOAT32,
                                              44100, 2, 64);
    state.tabs[1].session.currentFile = "/tmp/second.wav";
    state.tabs[1].session.document.initialize(cupuacu::SampleFormat::FLOAT32,
                                              48000, 1, 32);

    cupuacu::test::ensureSdlTtfInitialized();

    state.mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            &state, &state.getActiveDocumentSession(), &state.getActiveViewState(),
            "tabs", 800, 400, SDL_WINDOW_HIDDEN);
    cupuacu::gui::buildComponents(&state,
                                  state.mainDocumentSessionWindow->getWindow());

    auto *root = state.mainDocumentSessionWindow->getWindow()->getRootComponent();
    REQUIRE(root != nullptr);
    root->timerCallbackRecursive();

    auto *tabStrip =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::TabStrip>(
            root, "TabStrip");
    REQUIRE(tabStrip != nullptr);

    auto *secondButton =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::TabStripTab>(
            tabStrip, "TabStripTab:second.wav");
    REQUIRE(secondButton != nullptr);

    REQUIRE(secondButton->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    REQUIRE(secondButton->mouseUp(
        cupuacu::test::integration::leftMouseUp()));

    REQUIRE(state.activeTabIndex == 1);
    REQUIRE(state.mainDocumentSessionWindow->getDocumentSession() ==
            &state.tabs[1].session);
    REQUIRE(state.getActiveDocumentSession().document.getSampleRate() == 48000);
}

TEST_CASE("Tab strip integration can close tabs with the embedded close icon",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 64, 44100, 2, 800, 400);
    REQUIRE(cupuacu::actions::createNewDocumentInNewTab(
        &state, 48000, cupuacu::SampleFormat::PCM_S16, 1));
    REQUIRE(state.tabs.size() == 2);
    REQUIRE(state.activeTabIndex == 1);

    auto *root = state.mainDocumentSessionWindow->getWindow()->getRootComponent();
    REQUIRE(root != nullptr);
    root->timerCallbackRecursive();

    auto *tabStrip =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::TabStrip>(
            root, "TabStrip");
    REQUIRE(tabStrip != nullptr);

    auto *activeTab =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::TabStripTab>(
            tabStrip, "TabStripTab:Untitled");
    REQUIRE(activeTab != nullptr);

    clickTabClose(activeTab);
    root->timerCallbackRecursive();
    REQUIRE(state.tabs.size() == 1);
    REQUIRE(state.activeTabIndex == 0);
}

TEST_CASE("Repeated new document creation rebuilds waveform channels cleanly",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 16, 44100, 2, 800, 400);

    REQUIRE(state.waveforms.size() == 2);

    cupuacu::actions::createNewDocument(
        &state, 44100, cupuacu::SampleFormat::PCM_S16, 2);
    REQUIRE(state.getActiveDocumentSession().document.getChannelCount() == 2);
    REQUIRE(state.waveforms.size() == 2);

    cupuacu::actions::createNewDocument(
        &state, 48000, cupuacu::SampleFormat::PCM_S8, 1);
    REQUIRE(state.getActiveDocumentSession().document.getChannelCount() == 1);
    REQUIRE(state.waveforms.size() == 1);

    cupuacu::actions::createNewDocument(
        &state, 44100, cupuacu::SampleFormat::PCM_S16, 2);
    REQUIRE(state.getActiveDocumentSession().document.getChannelCount() == 2);
    REQUIRE(state.waveforms.size() == 2);
}

TEST_CASE("Empty stereo document click keeps triangle markers hidden",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 16, 44100, 2, 800, 400);

    cupuacu::actions::createNewDocument(
        &state, 44100, cupuacu::SampleFormat::PCM_S16, 2);

    auto *root = state.mainDocumentSessionWindow->getWindow()->getRootComponent();
    auto *mainView =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::MainView>(
            root, "MainView");
    auto *underlay = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::WaveformsUnderlay>(root, "WaveformsUnderlay");
    auto *cursorTop = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::TriangleMarker>(root, "TriangleMarker:CursorTop");
    auto *cursorBottom = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::TriangleMarker>(root, "TriangleMarker:CursorBottom");

    REQUIRE(mainView != nullptr);
    REQUIRE(underlay != nullptr);
    REQUIRE(cursorTop != nullptr);
    REQUIRE(cursorBottom != nullptr);
    REQUIRE(state.getActiveDocumentSession().document.getFrameCount() == 0);
    REQUIRE(state.getActiveViewState().samplesPerPixel ==
            Catch::Approx(0.0));

    mainView->timerCallback();
    REQUIRE_FALSE(cursorTop->isVisible());
    REQUIRE_FALSE(cursorBottom->isVisible());

    REQUIRE_FALSE(underlay->mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        20,
        underlay->getHeight() / 8,
        20.0f,
        static_cast<float>(underlay->getHeight() / 8),
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));

    mainView->timerCallback();
    REQUIRE_FALSE(cursorTop->isVisible());
    REQUIRE_FALSE(cursorBottom->isVisible());
    REQUIRE_FALSE(state.getActiveDocumentSession().selection.isActive());
    REQUIRE(state.getActiveDocumentSession().cursor == 0);
}

TEST_CASE("Mono waveform integration keeps channel selection unified across the full height",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 64, 44100, 1, 800, 400);

    auto *root = state.mainDocumentSessionWindow->getWindow()->getRootComponent();
    auto *underlay = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::WaveformsUnderlay>(root, "WaveformsUnderlay");
    REQUIRE(underlay != nullptr);

    auto &viewState = state.getActiveViewState();
    const int dragY = underlay->getHeight() * 3 / 4;

    REQUIRE(underlay->mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        20,
        dragY,
        20.0f,
        static_cast<float>(dragY),
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));

    REQUIRE(viewState.selectedChannels == cupuacu::SelectedChannels::BOTH);
    REQUIRE(viewState.hoveringOverChannels == cupuacu::SelectedChannels::BOTH);

    underlay->getWindow()->setCapturingComponent(underlay);

    REQUIRE(underlay->mouseMove(cupuacu::gui::MouseEvent{
        cupuacu::gui::MOVE,
        120,
        dragY,
        120.0f,
        static_cast<float>(dragY),
        100.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));

    const auto &selection = state.getActiveDocumentSession().selection;
    REQUIRE(selection.isActive());
    REQUIRE(selection.getLengthInt() > 0);
    REQUIRE(viewState.selectedChannels == cupuacu::SelectedChannels::BOTH);
    REQUIRE(viewState.hoveringOverChannels == cupuacu::SelectedChannels::BOTH);
}

TEST_CASE("Generate silence dialog integration inserts silence at the cursor",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 4, 44100, 1, 800, 400);

    auto &doc = state.getActiveDocumentSession().document;
    for (int i = 0; i < 4; ++i)
    {
        doc.setSample(0, i, static_cast<float>(i + 1), false);
    }
    state.getActiveDocumentSession().cursor = 2;

    state.generateSilenceDialogWindow.reset(
        new cupuacu::gui::GenerateSilenceDialogWindow(&state));
    REQUIRE(state.generateSilenceDialogWindow != nullptr);
    REQUIRE(state.generateSilenceDialogWindow->isOpen());

    auto *root = state.generateSilenceDialogWindow->getWindow()->getRootComponent();
    auto *durationInput = findFirstRecursive<cupuacu::gui::TextInput>(root);
    auto *unitDropdown = findFirstRecursive<cupuacu::gui::DropdownMenu>(root);
    auto *okButton =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::TextButton>(
            root, "TextButton:OK");
    REQUIRE(durationInput != nullptr);
    REQUIRE(unitDropdown != nullptr);

    durationInput->setText("2");
    unitDropdown->setSelectedIndex(0);
    clickButton(okButton);

    REQUIRE(doc.getFrameCount() == 6);
    REQUIRE(doc.getSample(0, 0) == 1.0f);
    REQUIRE(doc.getSample(0, 1) == 2.0f);
    REQUIRE(doc.getSample(0, 2) == 0.0f);
    REQUIRE(doc.getSample(0, 3) == 0.0f);
    REQUIRE(doc.getSample(0, 4) == 3.0f);
    REQUIRE(doc.getSample(0, 5) == 4.0f);
}

TEST_CASE("Generate silence dialog integration treats Enter as OK",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 4, 44100, 1, 800, 400);

    auto &doc = state.getActiveDocumentSession().document;
    for (int i = 0; i < 4; ++i)
    {
        doc.setSample(0, i, static_cast<float>(i + 1), false);
    }
    state.getActiveDocumentSession().cursor = 1;

    state.generateSilenceDialogWindow.reset(
        new cupuacu::gui::GenerateSilenceDialogWindow(&state));
    REQUIRE(state.generateSilenceDialogWindow != nullptr);
    REQUIRE(state.generateSilenceDialogWindow->isOpen());

    auto *root = state.generateSilenceDialogWindow->getWindow()->getRootComponent();
    auto *durationInput = findFirstRecursive<cupuacu::gui::TextInput>(root);
    auto *unitDropdown = findFirstRecursive<cupuacu::gui::DropdownMenu>(root);
    REQUIRE(durationInput != nullptr);
    REQUIRE(unitDropdown != nullptr);
    durationInput->setText("2");
    unitDropdown->setSelectedIndex(0);

    sendKeyDown(state.generateSilenceDialogWindow->getWindow(),
                SDL_SCANCODE_RETURN);

    REQUIRE(doc.getFrameCount() == 6);
    REQUIRE(doc.getSample(0, 0) == 1.0f);
    REQUIRE(doc.getSample(0, 1) == 0.0f);
    REQUIRE(doc.getSample(0, 2) == 0.0f);
    REQUIRE(doc.getSample(0, 3) == 2.0f);
    REQUIRE(doc.getSample(0, 4) == 3.0f);
    REQUIRE(doc.getSample(0, 5) == 4.0f);
}

TEST_CASE("Waveform integration toggles sample points with playback state",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 64);

    REQUIRE_FALSE(state.waveforms.empty());
    auto *waveform = state.waveforms.front();

    auto &viewState = state.getActiveViewState();
    viewState.samplesPerPixel = 0.01;
    viewState.sampleOffset = 0;
    waveform->setBounds(0, 0, 800, 60);
    REQUIRE(cupuacu::gui::Waveform::shouldShowSamplePoints(
        viewState.samplesPerPixel, state.pixelScale));
    waveform->updateSamplePoints();

    const auto initialCount = waveform->getChildren().size();
    REQUIRE(initialCount > 0);

    waveform->setPlaybackPosition(10);
    REQUIRE(waveform->getChildren().empty());

    waveform->setPlaybackPosition(-1);
    REQUIRE(waveform->getChildren().size() == initialCount);
}

TEST_CASE("Waveform integration preserves highlight when entering a sample point child",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 64);

    REQUIRE_FALSE(state.waveforms.empty());
    auto *waveform = state.waveforms.front();

    auto &viewState = state.getActiveViewState();
    viewState.samplesPerPixel = 0.01;
    viewState.sampleOffset = 0;
    waveform->setBounds(0, 0, 800, 60);
    REQUIRE(cupuacu::gui::Waveform::shouldShowSamplePoints(
        viewState.samplesPerPixel, state.pixelScale));
    waveform->updateSamplePoints();

    auto *samplePoint = firstSamplePoint(waveform);
    REQUIRE(samplePoint != nullptr);

    waveform->setSamplePosUnderCursor(7);
    state.mainDocumentSessionWindow->getWindow()->setComponentUnderMouse(
        samplePoint);
    viewState.hoveringOverChannels = cupuacu::SelectedChannels::LEFT;

    waveform->mouseLeave();

    REQUIRE(waveform->getSamplePosUnderCursor() == 7);
    REQUIRE(viewState.hoveringOverChannels == cupuacu::SelectedChannels::BOTH);
}

TEST_CASE("Sample point integration drags a sample and records an undoable",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 64);

    REQUIRE_FALSE(state.waveforms.empty());
    auto *waveform = state.waveforms.front();

    auto &viewState = state.getActiveViewState();
    viewState.samplesPerPixel = 0.01;
    viewState.sampleOffset = 0;
    waveform->setBounds(0, 0, 800, 60);
    REQUIRE(cupuacu::gui::Waveform::shouldShowSamplePoints(
        viewState.samplesPerPixel, state.pixelScale));
    waveform->updateSamplePoints();

    auto *samplePoint = firstSamplePoint(waveform);
    REQUIRE(samplePoint != nullptr);

    const auto sampleIndex = samplePoint->getSampleIndex();
    const auto oldValue = samplePoint->getSampleValue();

    REQUIRE(samplePoint->mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN, samplePoint->getXPos(), samplePoint->getYPos(),
        static_cast<float>(samplePoint->getXPos()),
        static_cast<float>(samplePoint->getYPos()), 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{true, false, false}, 1}));

    REQUIRE(samplePoint->mouseMove(cupuacu::gui::MouseEvent{
        cupuacu::gui::MOVE, samplePoint->getXPos(), samplePoint->getYPos(),
        static_cast<float>(samplePoint->getXPos()),
        static_cast<float>(samplePoint->getYPos()), 0.0f, -15.0f,
        cupuacu::gui::MouseButtonState{true, false, false}, 0}));

    REQUIRE(samplePoint->mouseUp(cupuacu::gui::MouseEvent{
        cupuacu::gui::UP, samplePoint->getXPos(), samplePoint->getYPos(),
        static_cast<float>(samplePoint->getXPos()),
        static_cast<float>(samplePoint->getYPos()), 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{true, false, false}, 1}));

    REQUIRE(state.getActiveUndoables().size() == 1);
    REQUIRE(state.getActiveDocumentSession().document.getSample(0, sampleIndex) !=
            Catch::Approx(oldValue));
}

TEST_CASE("Triangle marker integration updates cursor and selection while dragging",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 1024);

    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "triangle-window", 320, 200, SDL_WINDOW_HIDDEN);
    auto root =
        std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    root->setBounds(0, 0, 320, 200);

    auto *cursorMarker = root->emplaceChild<cupuacu::gui::TriangleMarker>(
        &state, cupuacu::gui::TriangleMarkerType::CursorTop);
    cursorMarker->setBounds(20, 0, 12, 12);

    auto *startMarker = root->emplaceChild<cupuacu::gui::TriangleMarker>(
        &state, cupuacu::gui::TriangleMarkerType::SelectionStartTop);
    startMarker->setBounds(40, 0, 12, 12);

    window->setRootComponent(std::move(root));

    auto &viewState = state.getActiveViewState();
    viewState.samplesPerPixel = 2.0;
    state.getActiveDocumentSession().cursor = 10;
    state.getActiveDocumentSession().selection.setValue1(300);
    state.getActiveDocumentSession().selection.setValue2(500);

    REQUIRE(cursorMarker->mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{true, false, false}, 1}));
    window->setCapturingComponent(cursorMarker);
    REQUIRE(cursorMarker->mouseMove(cupuacu::gui::MouseEvent{
        cupuacu::gui::MOVE, 0, 0, 40.0f, 0.0f, 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{true, false, false}, 0}));
    REQUIRE(state.getActiveDocumentSession().cursor != 10);

    REQUIRE(startMarker->mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{true, false, false}, 1}));
    window->setCapturingComponent(startMarker);
    REQUIRE(startMarker->mouseMove(cupuacu::gui::MouseEvent{
        cupuacu::gui::MOVE, 0, 0, 120.0f, 0.0f, 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{true, false, false}, 0}));
    REQUIRE(state.getActiveDocumentSession().selection.getStartInt() != 300);
}

TEST_CASE("Document marker integration opens editor on mouse up and clears capture",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 128);

    auto *mainWindow = state.mainDocumentSessionWindow->getWindow();
    auto *root = mainWindow ? mainWindow->getRootComponent() : nullptr;
    auto *mainView = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::MainView>(root, "MainView");
    REQUIRE(mainView != nullptr);

    const uint64_t markerId =
        state.getActiveDocumentSession().document.addMarker(10, "Kick");
    mainView->updateTriangleMarkerBounds();

    auto *handle = findTopMarkerHandle(root, markerId);
    REQUIRE(handle != nullptr);

    REQUIRE(mainWindow->handleMouseEvent(
        componentMouseEvent(handle, cupuacu::gui::DOWN, 2, true)));
    REQUIRE((state.markerEditorDialogWindow == nullptr ||
             !state.markerEditorDialogWindow->isOpen()));
    REQUIRE(state.getActiveUndoables().empty());

    REQUIRE(mainWindow->handleMouseEvent(
        componentMouseEvent(handle, cupuacu::gui::UP, 2, true)));
    if (state.markerEditorDialogWindow == nullptr)
    {
        SKIP("Marker editor dialog window unavailable in this SDL environment");
    }
    REQUIRE(state.markerEditorDialogWindow != nullptr);
    REQUIRE(state.markerEditorDialogWindow->isOpen());
    REQUIRE(state.getActiveUndoables().empty());
}

TEST_CASE("Closing marker editor restores main window hover updates",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 128);

    auto *mainWindow = state.mainDocumentSessionWindow->getWindow();
    auto *root = mainWindow ? mainWindow->getRootComponent() : nullptr;
    auto *mainView = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::MainView>(root, "MainView");
    auto *underlay = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::WaveformsUnderlay>(root, "WaveformsUnderlay");
    REQUIRE(mainView != nullptr);
    REQUIRE(underlay != nullptr);

    const uint64_t markerId =
        state.getActiveDocumentSession().document.addMarker(10, "Kick");
    mainView->updateTriangleMarkerBounds();

    auto *handle = findTopMarkerHandle(root, markerId);
    REQUIRE(handle != nullptr);

    auto *dialogWindow = openMarkerEditorOrSkip(state, mainWindow, handle);
    sendKeyDown(dialogWindow, SDL_SCANCODE_ESCAPE);
    processPendingSdlWindowEvents(&state);

    REQUIRE_FALSE(state.markerEditorDialogWindow->isOpen());
    REQUIRE(state.modalWindow == nullptr);

    moveMouseOverComponent(mainWindow, handle);
    REQUIRE(mainWindow->getComponentUnderMouse() == handle);
    REQUIRE(handle->getTooltipText() == "Marker at 10\nKick");

    moveMouseOverComponent(mainWindow, underlay);
    REQUIRE(mainWindow->getComponentUnderMouse() != nullptr);
    REQUIRE(mainWindow->getComponentUnderMouse() != handle);
}

TEST_CASE("Marker editor integration applies name and position as one undoable",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 128);

    auto *mainWindow = state.mainDocumentSessionWindow->getWindow();
    auto *root = mainWindow ? mainWindow->getRootComponent() : nullptr;
    auto *mainView = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::MainView>(root, "MainView");
    REQUIRE(mainView != nullptr);

    const uint64_t markerId =
        state.getActiveDocumentSession().document.addMarker(10, "Kick");
    mainView->updateTriangleMarkerBounds();

    auto *handle = findTopMarkerHandle(root, markerId);
    REQUIRE(handle != nullptr);
    auto *dialogWindow = openMarkerEditorOrSkip(state, mainWindow, handle);
    auto *dialogRoot = dialogWindow->getRootComponent();
    REQUIRE(dialogRoot != nullptr);

    auto inputs = findTextInputs(dialogRoot);
    REQUIRE(inputs.size() == 2);
    inputs[0]->setText("Snare");
    inputs[1]->setText("24");

    auto *okButton = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::TextButton>(dialogRoot, "TextButton:OK");
    REQUIRE(okButton != nullptr);
    clickComponentThroughWindow(dialogWindow, okButton);

    const auto &marker = state.getActiveDocumentSession().document.getMarkers()[0];
    REQUIRE(marker.id == markerId);
    REQUIRE(marker.frame == 24);
    REQUIRE(marker.label == "Snare");
    REQUIRE(state.getUndoDescription() == "Edit marker");

    state.undo();
    REQUIRE(state.getActiveDocumentSession().document.getMarkers()[0].frame == 10);
    REQUIRE(state.getActiveDocumentSession().document.getMarkers()[0].label ==
            "Kick");

    state.redo();
    REQUIRE(state.getActiveDocumentSession().document.getMarkers()[0].frame == 24);
    REQUIRE(state.getActiveDocumentSession().document.getMarkers()[0].label ==
            "Snare");
}

TEST_CASE("Marker editor integration delete is undoable one marker at a time",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    createBuiltSessionUi(&state, 128);

    auto *mainWindow = state.mainDocumentSessionWindow->getWindow();
    auto *root = mainWindow ? mainWindow->getRootComponent() : nullptr;
    auto *mainView = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::MainView>(root, "MainView");
    REQUIRE(mainView != nullptr);

    const uint64_t markerId =
        state.getActiveDocumentSession().document.addMarker(10, "Kick");
    mainView->updateTriangleMarkerBounds();

    auto *handle = findTopMarkerHandle(root, markerId);
    REQUIRE(handle != nullptr);
    auto *dialogWindow = openMarkerEditorOrSkip(state, mainWindow, handle);
    auto *dialogRoot = dialogWindow->getRootComponent();
    REQUIRE(dialogRoot != nullptr);

    auto *deleteButton = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::TextButton>(dialogRoot, "TextButton:Delete");
    REQUIRE(deleteButton != nullptr);
    clickComponentThroughWindow(dialogWindow, deleteButton);

    REQUIRE(state.getActiveDocumentSession().document.getMarkers().empty());
    REQUIRE(state.getUndoDescription() == "Delete marker");

    state.undo();
    REQUIRE(state.getActiveDocumentSession().document.getMarkers().size() == 1);
    REQUIRE(state.getActiveDocumentSession().document.getMarkers()[0].id == markerId);
    REQUIRE(state.getActiveDocumentSession().document.getMarkers()[0].frame == 10);
    REQUIRE(state.getActiveDocumentSession().document.getMarkers()[0].label ==
            "Kick");
}
