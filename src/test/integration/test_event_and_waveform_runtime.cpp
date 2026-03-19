#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "IntegrationTestHelpers.hpp"

#include "State.hpp"
#include "actions/ZoomPlanning.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/EventHandling.hpp"
#include "gui/Gui.hpp"
#include "gui/LabeledField.hpp"
#include "gui/SamplePoint.hpp"
#include "gui/StatusBar.hpp"
#include "gui/TriangleMarker.hpp"
#include "gui/Waveform.hpp"
#include "gui/Window.hpp"

#include <SDL3/SDL.h>

#include <memory>

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

    auto *statusBar = dynamic_cast<cupuacu::gui::StatusBar *>(state.statusBar);
    REQUIRE(statusBar != nullptr);

    auto *posField = findStatusField(statusBar, "Pos");
    auto *startField = findStatusField(statusBar, "St");
    auto *endField = findStatusField(statusBar, "End");
    auto *lengthField = findStatusField(statusBar, "Len");
    auto *valueField = findStatusField(statusBar, "Val");

    REQUIRE(posField != nullptr);
    REQUIRE(startField != nullptr);
    REQUIRE(endField != nullptr);
    REQUIRE(lengthField != nullptr);
    REQUIRE(valueField != nullptr);

    const auto initialPosBounds = posField->getBounds();
    const auto initialStartBounds = startField->getBounds();
    const auto initialEndBounds = endField->getBounds();
    const auto initialLengthBounds = lengthField->getBounds();
    const auto initialValueBounds = valueField->getBounds();

    REQUIRE(initialStartBounds.x > initialPosBounds.x);
    REQUIRE(initialEndBounds.x > initialStartBounds.x);
    REQUIRE(initialLengthBounds.x > initialEndBounds.x);
    REQUIRE(initialValueBounds.x > initialLengthBounds.x);
    REQUIRE(initialPosBounds.w == initialStartBounds.w);
    REQUIRE(initialStartBounds.w == initialEndBounds.w);
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
