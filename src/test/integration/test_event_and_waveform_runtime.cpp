#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "IntegrationTestHelpers.hpp"

#include "State.hpp"
#include "actions/DocumentLifecycle.hpp"
#include "actions/ZoomPlanning.hpp"
#include "gui/DropdownMenu.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/EventHandling.hpp"
#include "gui/GenerateSilenceDialogWindow.hpp"
#include "gui/Gui.hpp"
#include "gui/LabeledField.hpp"
#include "gui/NewFileDialogWindow.hpp"
#include "gui/SamplePoint.hpp"
#include "gui/StatusBar.hpp"
#include "gui/TextButton.hpp"
#include "gui/TextInput.hpp"
#include "gui/TriangleMarker.hpp"
#include "gui/Waveform.hpp"
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

        auto &session = state->activeDocumentSession;
        session.document.initialize(cupuacu::SampleFormat::FLOAT32, sampleRate,
                                    channels, frameCount);
        session.syncSelectionAndCursorToDocumentLength();

        state->mainDocumentSessionWindow =
            std::make_unique<cupuacu::gui::DocumentSessionWindow>(
                state, &session, "built-session", width, height,
                SDL_WINDOW_HIDDEN);
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

        SNDFILE *file = sf_open(path.string().c_str(), SFM_WRITE, &info);
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
    cupuacu::State state{};
    createBuiltSessionUi(&state, 4096);

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 8.0;
    viewState.sampleOffset = 160;
    state.activeDocumentSession.selection.setValue1(200);
    state.activeDocumentSession.selection.setValue2(500);

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
    cupuacu::State state{};
    createBuiltSessionUi(&state, 4096);

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
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

TEST_CASE("Event handling integration blocks interactive events behind modal",
          "[integration]")
{
    cupuacu::State state{};

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
            &state, &state.activeDocumentSession, "main-doc", 320, 240, 0);
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
    cupuacu::State state{};
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

TEST_CASE("Event handling integration returns success for quit and main-window close requests",
          "[integration]")
{
    SECTION("quit event exits immediately")
    {
        cupuacu::State state{};
        createBuiltSessionUi(&state, 256);

        SDL_Event event{};
        event.type = SDL_EVENT_QUIT;

        REQUIRE(cupuacu::gui::handleAppEvent(&state, &event) == SDL_APP_SUCCESS);
        REQUIRE(state.mainDocumentSessionWindow->getWindow()->isOpen());
    }

    SECTION("main document close request exits and closes the window")
    {
        cupuacu::State state{};
        createBuiltSessionUi(&state, 256);

        auto *mainWindow = state.mainDocumentSessionWindow->getWindow();
        state.windows.push_back(mainWindow);

        SDL_Event event{};
        event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        event.window.windowID = mainWindow->getId();

        REQUIRE(cupuacu::gui::handleAppEvent(&state, &event) == SDL_APP_SUCCESS);
        REQUIRE_FALSE(mainWindow->isOpen());
    }
}

TEST_CASE("Startup document restore integration reopens the most recent file",
          "[integration]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-startup-restore"));
    const auto wavPath = cleanup.path() / "startup.wav";
    writeTestWav(wavPath, 48000, 2, {0.1f, -0.1f, 0.2f, -0.2f, 0.3f, -0.3f});

    cupuacu::State state{};
    state.recentFiles = {wavPath.string()};
    createBuiltSessionUi(&state, 8);

    state.activeDocumentSession.currentFile = "before.wav";
    state.activeDocumentSession.selection.setValue1(1.0);
    state.activeDocumentSession.selection.setValue2(3.0);
    state.activeDocumentSession.cursor = 2;

    cupuacu::actions::restoreStartupDocument(&state);

    REQUIRE(state.activeDocumentSession.currentFile == wavPath.string());
    REQUIRE(state.activeDocumentSession.document.getSampleRate() == 48000);
    REQUIRE(state.activeDocumentSession.document.getChannelCount() == 2);
    REQUIRE(state.activeDocumentSession.document.getFrameCount() == 3);
    REQUIRE_FALSE(state.activeDocumentSession.selection.isActive());
    REQUIRE(state.activeDocumentSession.cursor == 0);
    REQUIRE(state.recentFiles == std::vector<std::string>{wavPath.string()});
}

TEST_CASE("Event handling integration ignores unfocused key and text input",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    state.mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            &state, &state.activeDocumentSession, "main-doc", 320, 240,
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
    cupuacu::State state{};
    createBuiltSessionUi(&state, 1024);

    auto *mainWindow = state.mainDocumentSessionWindow->getWindow();
    REQUIRE_FALSE(state.waveforms.empty());
    auto *waveform = state.waveforms.front();

    waveform->setSamplePosUnderCursor(42);
    mainWindow->setComponentUnderMouse(waveform);
    state.activeDocumentSession.selection.reset();

    cupuacu::gui::handleWindowMouseLeave(&state, mainWindow);
    REQUIRE_FALSE(waveform->getSamplePosUnderCursor().has_value());
    REQUIRE(mainWindow->getComponentUnderMouse() == nullptr);
}

TEST_CASE("Window integration routes focused key input to focused component",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
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
    cupuacu::State state{};
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
    cupuacu::State state{};
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
    state.activeDocumentSession.cursor = 321;

    statusBar->timerCallback();

    REQUIRE(posField->isDirty());
    REQUIRE(startField->isDirty());
}

TEST_CASE("Status bar integration shows sample rate and bit depth",
          "[integration]")
{
    cupuacu::State state{};
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

TEST_CASE("Status bar integration shows persisted integer sample values for PCM",
          "[integration]")
{
    cupuacu::State state{};
    createBuiltSessionUi(&state, 32, 44100, 1, 800, 400);

    auto *statusBar = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::StatusBar>(
        state.mainDocumentSessionWindow->getWindow()->getContentLayer(),
        "StatusBar");
    REQUIRE(statusBar != nullptr);

    auto *valueField = findStatusField(statusBar, "Val");
    REQUIRE(valueField != nullptr);

    auto &session = state.activeDocumentSession;

    SECTION("edited PCM16 samples use the quantized writer code")
    {
        session.document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 1);
        session.document.setSample(0, 0, -1.0f);
        state.mainDocumentSessionWindow->getViewState().sampleValueUnderMouseCursor =
            cupuacu::gui::HoveredSampleInfo{-1.0f, 0, 0};

        statusBar->timerCallback();

        REQUIRE(valueField->getValue() == "-32767");
    }

    SECTION("untouched loaded PCM16 samples preserve the original sample code")
    {
        session.document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 1);
        session.document.setSample(0, 0, -1.0f, false);
        state.mainDocumentSessionWindow->getViewState().sampleValueUnderMouseCursor =
            cupuacu::gui::HoveredSampleInfo{-1.0f, 0, 0};

        statusBar->timerCallback();

        REQUIRE(valueField->getValue() == "-32768");
    }

    SECTION("PCM8 uses the true signed 8-bit range")
    {
        session.document.initialize(cupuacu::SampleFormat::PCM_S8, 44100, 1, 1);
        session.document.setSample(0, 0, 1.0f);
        state.mainDocumentSessionWindow->getViewState().sampleValueUnderMouseCursor =
            cupuacu::gui::HoveredSampleInfo{1.0f, 0, 0};

        statusBar->timerCallback();

        REQUIRE(valueField->getValue() == "127");
    }
}

TEST_CASE("New file dialog integration creates an empty document with the selected format",
          "[integration]")
{
    cupuacu::State state{};
    createBuiltSessionUi(&state, 16, 44100, 2, 800, 400);

    state.newFileDialogWindow.reset(new cupuacu::gui::NewFileDialogWindow(&state));
    REQUIRE(state.newFileDialogWindow != nullptr);
    REQUIRE(state.newFileDialogWindow->isOpen());

    auto *root = state.newFileDialogWindow->getWindow()->getRootComponent();
    std::vector<cupuacu::gui::DropdownMenu *> dropdowns;
    collectRecursive(root, dropdowns);
    REQUIRE(dropdowns.size() >= 2);
    dropdowns[0]->setSelectedIndex(4);
    dropdowns[1]->setSelectedIndex(0);

    auto *okButton =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::TextButton>(
            root, "TextButton:OK");
    clickButton(okButton);

    auto &session = state.activeDocumentSession;
    REQUIRE(session.currentFile.empty());
    REQUIRE(session.document.getSampleRate() == 96000);
    REQUIRE(session.document.getSampleFormat() == cupuacu::SampleFormat::PCM_S8);
    REQUIRE(session.document.getChannelCount() == 2);
    REQUIRE(session.document.getFrameCount() == 0);
}

TEST_CASE("Generate silence dialog integration inserts silence at the cursor",
          "[integration]")
{
    cupuacu::State state{};
    createBuiltSessionUi(&state, 4, 44100, 1, 800, 400);

    auto &doc = state.activeDocumentSession.document;
    for (int i = 0; i < 4; ++i)
    {
        doc.setSample(0, i, static_cast<float>(i + 1), false);
    }
    state.activeDocumentSession.cursor = 2;

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

TEST_CASE("Waveform integration toggles sample points with playback state",
          "[integration]")
{
    cupuacu::State state{};
    createBuiltSessionUi(&state, 64);

    REQUIRE_FALSE(state.waveforms.empty());
    auto *waveform = state.waveforms.front();

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
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
    cupuacu::State state{};
    createBuiltSessionUi(&state, 64);

    REQUIRE_FALSE(state.waveforms.empty());
    auto *waveform = state.waveforms.front();

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
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
    cupuacu::State state{};
    createBuiltSessionUi(&state, 64);

    REQUIRE_FALSE(state.waveforms.empty());
    auto *waveform = state.waveforms.front();

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
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

    REQUIRE(state.undoables.size() == 1);
    REQUIRE(state.activeDocumentSession.document.getSample(0, sampleIndex) !=
            Catch::Approx(oldValue));
}

TEST_CASE("Triangle marker integration updates cursor and selection while dragging",
          "[integration]")
{
    cupuacu::State state{};
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

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 2.0;
    state.activeDocumentSession.cursor = 10;
    state.activeDocumentSession.selection.setValue1(300);
    state.activeDocumentSession.selection.setValue2(500);

    REQUIRE(cursorMarker->mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{true, false, false}, 1}));
    window->setCapturingComponent(cursorMarker);
    REQUIRE(cursorMarker->mouseMove(cupuacu::gui::MouseEvent{
        cupuacu::gui::MOVE, 0, 0, 40.0f, 0.0f, 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{true, false, false}, 0}));
    REQUIRE(state.activeDocumentSession.cursor != 10);

    REQUIRE(startMarker->mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{true, false, false}, 1}));
    window->setCapturingComponent(startMarker);
    REQUIRE(startMarker->mouseMove(cupuacu::gui::MouseEvent{
        cupuacu::gui::MOVE, 0, 0, 120.0f, 0.0f, 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{true, false, false}, 0}));
    REQUIRE(state.activeDocumentSession.selection.getStartInt() != 300);
}
