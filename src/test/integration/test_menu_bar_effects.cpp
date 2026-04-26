#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "IntegrationTestHelpers.hpp"
#include "../TestPaths.hpp"

#include "effects/AmplifyEnvelopeEffect.hpp"
#include "effects/AmplifyFadeEffect.hpp"
#include "effects/DynamicsEffect.hpp"
#include "effects/RemoveSilenceEffect.hpp"
#include "effects/ReverseEffect.hpp"
#include "State.hpp"
#include "actions/effects/BackgroundEffect.hpp"
#include "SelectedChannels.hpp"
#include "audio/AudioDevices.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/ControlPointHandle.hpp"
#include "gui/DocumentSessionWindow.hpp"
#include "gui/DropdownMenu.hpp"
#include "gui/EventHandling.hpp"
#include "gui/Label.hpp"
#include "gui/Menu.hpp"
#include "gui/MenuBar.hpp"
#include "gui/Slider.hpp"
#include "gui/TextButton.hpp"
#include "gui/TextInput.hpp"
#include "gui/WaveformSamplePointPlanning.hpp"
#include "gui/Window.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>

using Catch::Approx;

namespace
{
    struct EffectsMenuHarness
    {
        std::unique_ptr<cupuacu::gui::Window> window;
        cupuacu::gui::MenuBar *menuBar = nullptr;
        cupuacu::gui::Menu *reverseMenu = nullptr;
        cupuacu::gui::Menu *amplifyFadeMenu = nullptr;
        cupuacu::gui::Menu *amplifyEnvelopeMenu = nullptr;
        cupuacu::gui::Menu *dynamicsMenu = nullptr;
        cupuacu::gui::Menu *removeSilenceMenu = nullptr;
    };

    EffectsMenuHarness createEffectsMenuHarness(cupuacu::State *state)
    {
        EffectsMenuHarness harness{};
        cupuacu::test::ensureTestPaths(*state, "menu-bar-effects");
        if (!state->mainDocumentSessionWindow)
        {
            state->mainDocumentSessionWindow =
                std::make_unique<cupuacu::gui::DocumentSessionWindow>(
                    state, &state->getActiveDocumentSession(),
                    &state->getActiveViewState(), "main", 640, 360,
                    SDL_WINDOW_HIDDEN);
            state->windows.push_back(
                state->mainDocumentSessionWindow->getWindow());
        }

        harness.window = std::make_unique<cupuacu::gui::Window>(
            state, "menu-bar-effects", 480, 240, SDL_WINDOW_HIDDEN);

        auto root =
            std::make_unique<cupuacu::test::integration::RootComponent>(state);
        harness.menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(state);
        root->setBounds(0, 0, 480, 240);
        harness.menuBar->setBounds(0, 0, 480, 40);
        harness.window->setRootComponent(std::move(root));
        harness.window->setMenuBar(harness.menuBar);

        auto topLevelMenus =
            cupuacu::test::integration::menuChildren(harness.menuBar);
        REQUIRE(topLevelMenus.size() == 6);
        auto *effectsMenu = topLevelMenus[4];
        auto effectSubMenus =
            cupuacu::test::integration::menuChildren(effectsMenu);
        REQUIRE(effectSubMenus.size() == 5);
        harness.reverseMenu = effectSubMenus[0];
        harness.amplifyFadeMenu = effectSubMenus[1];
        harness.amplifyEnvelopeMenu = effectSubMenus[2];
        harness.dynamicsMenu = effectSubMenus[3];
        harness.removeSilenceMenu = effectSubMenus[4];
        return harness;
    }

    void clickButton(cupuacu::gui::TextButton *button)
    {
        REQUIRE(button != nullptr);
        REQUIRE(button->mouseDown(cupuacu::test::integration::leftMouseDown()));
        REQUIRE(button->mouseUp(cupuacu::test::integration::leftMouseUp()));
        if (auto *window = button->getWindow())
        {
            window->handleMouseEvent(cupuacu::gui::MouseEvent{
                cupuacu::gui::MOVE, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f,
                cupuacu::gui::MouseButtonState{false, false, false}, 0});
        }
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

    void processPendingSdlWindowEvents(cupuacu::State *state)
    {
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            cupuacu::gui::handleAppEvent(state, &event);
        }
    }

    void drainPendingEffectWork(cupuacu::State *state)
    {
        for (int attempt = 0; attempt < 5000; ++attempt)
        {
            cupuacu::actions::effects::processPendingEffectWork(state);
            if (!state->backgroundEffectJob)
            {
                cupuacu::actions::effects::processPendingEffectWork(state);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        FAIL("Timed out waiting for background effect work");
    }

    SDL_Event makeMouseButtonEvent(const Uint32 type,
                                   cupuacu::gui::Window *window, const float x,
                                   const float y, const bool leftDown,
                                   const uint8_t clicks = 1)
    {
        SDL_Event event{};
        event.type = type;
        event.button.windowID = window != nullptr ? window->getId() : 0;
        event.button.x = x;
        event.button.y = y;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = leftDown;
        event.button.clicks = clicks;
        return event;
    }

    void initializeActiveDocument(cupuacu::State *state)
    {
        state->getActiveDocumentSession().document.initialize(
            cupuacu::SampleFormat::FLOAT32, 44100, 1, 3);
    }
} // namespace

TEST_CASE(
    "Effects menu integration opens AmplifyFade Amplify Envelope Dynamics and "
    "Remove silence dialogs",
    "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::test::StateWithTestPaths state{};
    initializeActiveDocument(&state);
    auto harness = createEffectsMenuHarness(&state);

    REQUIRE(state.amplifyFadeDialog == nullptr);
    REQUIRE(harness.amplifyFadeMenu->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.amplifyFadeDialog != nullptr);
    REQUIRE(state.amplifyFadeDialog->isOpen());
    REQUIRE(state.modalWindow == state.amplifyFadeDialog->getWindow());
    REQUIRE(std::count(state.windows.begin(), state.windows.end(),
                       state.amplifyFadeDialog->getWindow()) == 1);

    REQUIRE(state.amplifyEnvelopeDialog == nullptr);
    REQUIRE(harness.amplifyEnvelopeMenu->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.amplifyEnvelopeDialog != nullptr);
    REQUIRE(state.amplifyEnvelopeDialog->isOpen());

    REQUIRE(state.dynamicsDialog == nullptr);
    REQUIRE(harness.dynamicsMenu->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.dynamicsDialog != nullptr);
    REQUIRE(state.dynamicsDialog->isOpen());

    REQUIRE(state.removeSilenceDialog == nullptr);
    REQUIRE(harness.removeSilenceMenu->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.removeSilenceDialog != nullptr);
    REQUIRE(state.removeSilenceDialog->isOpen());
}

TEST_CASE(
    "Amplify Envelope dialog preview adds and removes nodes from the drawable "
    "envelope",
    "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::test::StateWithTestPaths state{};
    initializeActiveDocument(&state);
    auto harness = createEffectsMenuHarness(&state);
    REQUIRE(harness.amplifyEnvelopeMenu->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.amplifyEnvelopeDialog != nullptr);
    REQUIRE(state.amplifyEnvelopeDialog->isOpen());

    auto *dialogWindow = state.amplifyEnvelopeDialog->getWindow();
    REQUIRE(dialogWindow != nullptr);
    auto *root = dialogWindow->getRootComponent();
    REQUIRE(root != nullptr);
    auto *preview = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::Component>(root, "AmplifyEnvelopePreview");
    REQUIRE(preview != nullptr);

    auto countEnvelopeNodes = [&](cupuacu::gui::Component *component,
                                  auto &&countNodesRef) -> int
    {
        int count = 0;
        for (const auto &child : component->getChildren())
        {
            if (dynamic_cast<cupuacu::gui::ControlPointHandle *>(child.get()) !=
                    nullptr &&
                child->getComponentName().rfind("AmplifyEnvelopeNode:", 0) == 0)
            {
                ++count;
            }
            count += countNodesRef(child.get(), countNodesRef);
        }
        return count;
    };

    REQUIRE(countEnvelopeNodes(preview, countEnvelopeNodes) == 2);

    const SDL_Rect previewBounds = preview->getBounds();
    const int handleSize = cupuacu::gui::getWaveformSamplePointSize(
        state.pixelScale, state.uiScale);
    const float top = previewBounds.y + handleSize * 0.5f;
    const float bottom = previewBounds.y +
                         std::max(0, previewBounds.h - handleSize) +
                         handleSize * 0.5f;
    const float defaultLineY = bottom - (bottom - top) * 0.1f;
    const float clickX = previewBounds.x + previewBounds.w * 0.5f;

    SDL_Event down = makeMouseButtonEvent(
        SDL_EVENT_MOUSE_BUTTON_DOWN, dialogWindow, clickX, defaultLineY, true);
    SDL_Event up = makeMouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_UP, dialogWindow,
                                        clickX, defaultLineY, true);
    REQUIRE(cupuacu::gui::handleAppEvent(&state, &down) == SDL_APP_CONTINUE);
    REQUIRE(cupuacu::gui::handleAppEvent(&state, &up) == SDL_APP_CONTINUE);
    REQUIRE(countEnvelopeNodes(preview, countEnvelopeNodes) == 3);

    auto *newHandle = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::ControlPointHandle>(root, "AmplifyEnvelopeNode:1");
    REQUIRE(newHandle != nullptr);
    const SDL_Rect nodeBounds = newHandle->getAbsoluteBounds();
    down = makeMouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, dialogWindow,
                                nodeBounds.x + nodeBounds.w * 0.5f,
                                nodeBounds.y + nodeBounds.h * 0.5f, true, 2);
    up = makeMouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_UP, dialogWindow,
                              nodeBounds.x + nodeBounds.w * 0.5f,
                              nodeBounds.y + nodeBounds.h * 0.5f, true, 2);
    REQUIRE(cupuacu::gui::handleAppEvent(&state, &down) == SDL_APP_CONTINUE);
    REQUIRE(cupuacu::gui::handleAppEvent(&state, &up) == SDL_APP_CONTINUE);
    REQUIRE(countEnvelopeNodes(preview, countEnvelopeNodes) == 2);
}

TEST_CASE(
    "Amplify Envelope dialog integration wires snap fade length and fade "
    "preset controls",
    "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().document.initialize(
        cupuacu::SampleFormat::FLOAT32, 1000, 1, 1000);
    state.getActiveDocumentSession().selection.setHighest(1000.0);
    state.getActiveDocumentSession().selection.setValue1(0.0);
    state.getActiveDocumentSession().selection.setValue2(1000.0);

    auto harness = createEffectsMenuHarness(&state);
    REQUIRE(harness.amplifyEnvelopeMenu->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.amplifyEnvelopeDialog != nullptr);

    auto *window = state.amplifyEnvelopeDialog->getWindow();
    auto *root = window->getRootComponent();
    REQUIRE(root != nullptr);

    auto *snapButton = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::TextButton>(root, "TextButton:Snap");
    auto *fadeButton = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::TextButton>(root, "TextButton:Fade in & out");
    auto *fadeLengthInput = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::TextInput>(root, "TextInput");
    REQUIRE(snapButton != nullptr);
    REQUIRE(fadeButton != nullptr);
    REQUIRE(fadeLengthInput != nullptr);

    clickButton(snapButton);
    REQUIRE(state.effectSettings.amplifyEnvelope.snapEnabled);

    window->setFocusedComponent(fadeLengthInput);
    fadeLengthInput->setText("250");
    window->setFocusedComponent(nullptr);
    REQUIRE(state.effectSettings.amplifyEnvelope.fadeLengthMs == Approx(250.0));

    clickButton(fadeButton);
    const auto &settings = state.amplifyEnvelopeDialog->getSettings();
    REQUIRE(settings.points.size() == 4);
    REQUIRE(settings.points[0].percent == Approx(0.0));
    REQUIRE(settings.points[1].position == Approx(0.25));
    REQUIRE(settings.points[1].percent == Approx(100.0));
    REQUIRE(settings.points[2].position == Approx(0.75));
    REQUIRE(settings.points[2].percent == Approx(100.0));
    REQUIRE(settings.points[3].percent == Approx(0.0));
}

TEST_CASE("Amplify Envelope shift-drag locks movement to the dominant axis",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().document.initialize(
        cupuacu::SampleFormat::FLOAT32, 44100, 1, 1000);
    auto harness = createEffectsMenuHarness(&state);
    REQUIRE(harness.amplifyEnvelopeMenu->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.amplifyEnvelopeDialog != nullptr);

    auto *window = state.amplifyEnvelopeDialog->getWindow();
    auto *root = window->getRootComponent();
    auto *preview = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::Component>(root, "AmplifyEnvelopePreview");
    REQUIRE(preview != nullptr);

    const SDL_Rect previewBounds = preview->getBounds();
    const int handleSize = cupuacu::gui::getWaveformSamplePointSize(
        state.pixelScale, state.uiScale);
    const float top = handleSize * 0.5f;
    const float bottom =
        std::max(0, previewBounds.h - handleSize) + handleSize * 0.5f;
    const float defaultLineY = bottom - (bottom - top) * 0.1f;
    const float clickX = previewBounds.w * 0.5f;

    REQUIRE(preview->mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN, static_cast<int32_t>(clickX),
        static_cast<int32_t>(defaultLineY), clickX, defaultLineY, 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{true, false, false}, 1}));
    REQUIRE(preview->mouseUp(cupuacu::gui::MouseEvent{
        cupuacu::gui::UP, static_cast<int32_t>(clickX),
        static_cast<int32_t>(defaultLineY), clickX, defaultLineY, 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{true, false, false}, 1}));

    auto baseline = state.amplifyEnvelopeDialog->getSettings();
    REQUIRE(baseline.points.size() == 3);
    const double originalPosition = baseline.points[1].position;
    const double originalPercent = baseline.points[1].percent;

    auto *node = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::ControlPointHandle>(root, "AmplifyEnvelopeNode:1");
    REQUIRE(node != nullptr);
    const SDL_Rect nodeBounds = node->getBounds();
    const float startX = nodeBounds.x + nodeBounds.w * 0.5f;
    const float startY = nodeBounds.y + nodeBounds.h * 0.5f;

    REQUIRE(preview->mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN, static_cast<int32_t>(startX),
        static_cast<int32_t>(startY), startX, startY, 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{true, false, false}, 1}));
    REQUIRE(preview->mouseMove(cupuacu::gui::MouseEvent{
        cupuacu::gui::MOVE, static_cast<int32_t>(startX + 120.0f),
        static_cast<int32_t>(startY + 10.0f), startX + 120.0f, startY + 10.0f,
        120.0f, 10.0f, cupuacu::gui::MouseButtonState{true, false, false}, 1,
        0.0f, 0.0f, SDL_KMOD_SHIFT}));
    REQUIRE(preview->mouseUp(cupuacu::gui::MouseEvent{
        cupuacu::gui::UP, static_cast<int32_t>(startX + 120.0f),
        static_cast<int32_t>(startY + 10.0f), startX + 120.0f, startY + 10.0f,
        0.0f, 0.0f, cupuacu::gui::MouseButtonState{true, false, false}, 1, 0.0f,
        0.0f, SDL_KMOD_SHIFT}));

    const auto &shifted = state.amplifyEnvelopeDialog->getSettings();
    REQUIRE(shifted.points[1].position != Approx(originalPosition));
    REQUIRE(shifted.points[1].percent == Approx(originalPercent));
}

TEST_CASE("Remove silence mode dropdown selects from popup while modal is open",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::test::StateWithTestPaths state{};
    initializeActiveDocument(&state);
    auto harness = createEffectsMenuHarness(&state);
    REQUIRE(harness.removeSilenceMenu->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.removeSilenceDialog != nullptr);
    REQUIRE(state.removeSilenceDialog->isOpen());

    auto *dialogWindow = state.removeSilenceDialog->getWindow();
    REQUIRE(dialogWindow != nullptr);
    auto *root = dialogWindow->getRootComponent();
    REQUIRE(root != nullptr);

    std::vector<cupuacu::gui::DropdownMenu *> dropdowns;
    for (const auto &child : root->getChildren())
    {
        if (auto *dropdown =
                dynamic_cast<cupuacu::gui::DropdownMenu *>(child.get()))
        {
            dropdowns.push_back(dropdown);
        }
    }

    REQUIRE(dropdowns.size() >= 2);
    auto *modeDropdown = dropdowns[0];
    REQUIRE(modeDropdown->getSelectedIndex() == 0);

    const SDL_Rect bounds = modeDropdown->getBounds();
    SDL_Event down = makeMouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN,
                                          dialogWindow, bounds.x + 12.0f,
                                          bounds.y + bounds.h * 0.5f, true);
    SDL_Event up = makeMouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_UP, dialogWindow,
                                        bounds.x + 12.0f,
                                        bounds.y + bounds.h * 0.5f, true);
    REQUIRE(cupuacu::gui::handleAppEvent(&state, &down) == SDL_APP_CONTINUE);
    REQUIRE(cupuacu::gui::handleAppEvent(&state, &up) == SDL_APP_CONTINUE);
    REQUIRE(modeDropdown->isExpanded());

    cupuacu::gui::Window *popupWindow = nullptr;
    for (auto *candidate : state.windows)
    {
        if (candidate != dialogWindow &&
            candidate != state.mainDocumentSessionWindow->getWindow())
        {
            if (candidate->getRootComponent() != nullptr &&
                dynamic_cast<cupuacu::gui::DropdownOwnerComponent *>(
                    candidate->getRootComponent()) != nullptr)
            {
                popupWindow = candidate;
                break;
            }
        }
    }

    REQUIRE(popupWindow != nullptr);
    const int popupSelectX = 40;
    const int popupSelectY = modeDropdown->getPopupRowHeight() + 10;
    down = makeMouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, popupWindow,
                                static_cast<float>(popupSelectX),
                                static_cast<float>(popupSelectY), true);
    up = makeMouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_UP, popupWindow,
                              static_cast<float>(popupSelectX),
                              static_cast<float>(popupSelectY), true);
    REQUIRE(cupuacu::gui::handleAppEvent(&state, &down) == SDL_APP_CONTINUE);
    REQUIRE(cupuacu::gui::handleAppEvent(&state, &up) == SDL_APP_CONTINUE);

    REQUIRE_FALSE(modeDropdown->isExpanded());
    REQUIRE(modeDropdown->getSelectedIndex() == 1);
    REQUIRE(state.effectSettings.removeSilence.modeIndex == 1);
}

TEST_CASE(
    "Reverse menu integration applies immediately without opening a dialog",
    "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::test::StateWithTestPaths state{};
    auto ui = cupuacu::test::integration::createSessionUi(&state, 5, false, 2);
    auto &session = state.getActiveDocumentSession();
    auto &doc = session.document;

    for (int64_t frame = 0; frame < doc.getFrameCount(); ++frame)
    {
        doc.setSample(0, frame, static_cast<float>(frame), false);
        doc.setSample(1, frame, static_cast<float>(10 + frame), false);
    }

    session.selection.setValue1(1.0);
    session.selection.setValue2(4.0);
    state.getActiveViewState().selectedChannels =
        cupuacu::SelectedChannels::LEFT;

    auto harness = createEffectsMenuHarness(&state);
    REQUIRE(harness.reverseMenu->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    drainPendingEffectWork(&state);

    REQUIRE(state.modalWindow == nullptr);
    REQUIRE(state.getActiveUndoables().size() == 1);
    REQUIRE(doc.getSample(0, 0) == Approx(0.0f));
    REQUIRE(doc.getSample(0, 1) == Approx(3.0f));
    REQUIRE(doc.getSample(0, 2) == Approx(2.0f));
    REQUIRE(doc.getSample(0, 3) == Approx(1.0f));
    REQUIRE(doc.getSample(0, 4) == Approx(4.0f));
    REQUIRE(doc.getSample(1, 0) == Approx(10.0f));
    REQUIRE(doc.getSample(1, 1) == Approx(11.0f));
    REQUIRE(doc.getSample(1, 2) == Approx(12.0f));
    REQUIRE(doc.getSample(1, 3) == Approx(13.0f));
    REQUIRE(doc.getSample(1, 4) == Approx(14.0f));
}

TEST_CASE("AmplifyFade dialog integration exposes shared controls and presets",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::test::StateWithTestPaths state{};
    initializeActiveDocument(&state);
    auto harness = createEffectsMenuHarness(&state);
    REQUIRE(harness.amplifyFadeMenu->mouseDown(
        cupuacu::test::integration::leftMouseDown()));

    auto *root = state.amplifyFadeDialog->getWindow()->getRootComponent();
    REQUIRE(root != nullptr);

    auto *previewButton = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::TextButton>(root, "TextButton:Preview");
    auto *curveLabel =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::Label>(
            root, "Label: Curve");
    auto *curveDropdown = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::DropdownMenu>(root, "DropdownMenu");
    auto *resetButton = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::TextButton>(root, "TextButton:Reset");
    auto *lockButton = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::TextButton>(root, "TextButton:Lock");
    auto *normalizeButton = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::TextButton>(root, "TextButton:Normalize");
    auto *fadeInButton = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::TextButton>(root, "TextButton:Fade in");
    auto *fadeOutButton = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::TextButton>(root, "TextButton:Fade out");

    std::vector<cupuacu::gui::Slider *> sliders;
    for (const auto &child : root->getChildren())
    {
        if (auto *slider = dynamic_cast<cupuacu::gui::Slider *>(child.get()))
        {
            sliders.push_back(slider);
        }
    }

    REQUIRE(previewButton != nullptr);
    REQUIRE(curveLabel != nullptr);
    REQUIRE(curveDropdown != nullptr);
    REQUIRE(resetButton != nullptr);
    REQUIRE(lockButton != nullptr);
    REQUIRE(normalizeButton != nullptr);
    REQUIRE(fadeInButton != nullptr);
    REQUIRE(fadeOutButton != nullptr);
    REQUIRE(sliders.size() == 2);

    const SDL_Rect labelBounds = curveLabel->getBounds();
    const SDL_Rect dropdownBounds = curveDropdown->getBounds();
    REQUIRE(labelBounds.x + labelBounds.w < dropdownBounds.x);
    REQUIRE(std::abs((labelBounds.y + labelBounds.h / 2) -
                     (dropdownBounds.y + dropdownBounds.h / 2)) <= 1);

    REQUIRE(
        fadeOutButton->mouseDown(cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.amplifyFadeDialog->getStartPercent() == 100.0);
    REQUIRE(state.amplifyFadeDialog->getEndPercent() == 0.0);

    REQUIRE(
        fadeInButton->mouseDown(cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.amplifyFadeDialog->getStartPercent() == 0.0);
    REQUIRE(state.amplifyFadeDialog->getEndPercent() == 100.0);

    REQUIRE(lockButton->mouseDown(cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.amplifyFadeDialog->isLocked());

    state.getActiveDocumentSession().document.initialize(
        cupuacu::SampleFormat::FLOAT32, 44100, 1, 3);
    state.getActiveDocumentSession().document.setSample(0, 0, 0.25f, false);
    state.getActiveDocumentSession().document.setSample(0, 1, -0.5f, false);
    state.getActiveDocumentSession().document.setSample(0, 2, 0.1f, false);
    REQUIRE(normalizeButton->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.amplifyFadeDialog->getStartPercent() == Approx(200.0));
    REQUIRE(state.amplifyFadeDialog->getEndPercent() == Approx(200.0));

    REQUIRE(
        resetButton->mouseDown(cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.amplifyFadeDialog->getStartPercent() == 100.0);
    REQUIRE(state.amplifyFadeDialog->getEndPercent() == 100.0);
}

TEST_CASE(
    "AmplifyFade dialog preview toggles playback and restarts from the "
    "selection start",
    "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    auto ui = cupuacu::test::integration::createSessionUi(&state, 64, true);
    auto &session = state.getActiveDocumentSession();
    session.selection.setValue1(10.0);
    session.selection.setValue2(30.0);

    auto harness = createEffectsMenuHarness(&state);
    REQUIRE(harness.amplifyFadeMenu->mouseDown(
        cupuacu::test::integration::leftMouseDown()));

    auto *previewButton = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::TextButton>(
        state.amplifyFadeDialog->getWindow()->getRootComponent(),
        "TextButton:Preview");

    clickButton(previewButton);
    state.audioDevices->drainQueue();

    REQUIRE(state.audioDevices->isPlaying());
    REQUIRE(state.playbackRangeStart == 10);
    REQUIRE(state.playbackRangeEnd == 30);
    REQUIRE(state.audioDevices->getPlaybackPosition() == 10);

    std::vector<float> output(8, 0.0f);
    state.audioDevices->processCallbackCycle(nullptr, output.data(), 4);
    REQUIRE(state.audioDevices->getPlaybackPosition() == 14);

    clickButton(previewButton);
    state.audioDevices->drainQueue();
    REQUIRE_FALSE(state.audioDevices->isPlaying());

    clickButton(previewButton);
    state.audioDevices->drainQueue();
    REQUIRE(state.audioDevices->isPlaying());
    REQUIRE(state.audioDevices->getPlaybackPosition() == 10);

    clickButton(previewButton);
    state.audioDevices->drainQueue();
    REQUIRE_FALSE(state.audioDevices->isPlaying());
}

TEST_CASE(
    "AmplifyFade dialog apply closes and respects the selected channel target",
    "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    auto ui = cupuacu::test::integration::createSessionUi(&state, 8, false, 2);
    auto &session = state.getActiveDocumentSession();
    auto &doc = session.document;

    for (int64_t frame = 0; frame < doc.getFrameCount(); ++frame)
    {
        doc.setSample(0, frame, 1.0f, false);
        doc.setSample(1, frame, 1.0f, false);
    }

    session.selection.setValue1(2.0);
    session.selection.setValue2(6.0);
    state.getActiveViewState().selectedChannels =
        cupuacu::SelectedChannels::LEFT;

    auto harness = createEffectsMenuHarness(&state);
    REQUIRE(harness.amplifyFadeMenu->mouseDown(
        cupuacu::test::integration::leftMouseDown()));

    auto *root = state.amplifyFadeDialog->getWindow()->getRootComponent();
    auto *fadeOutButton = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::TextButton>(root, "TextButton:Fade out");
    auto *applyButton = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::TextButton>(root, "TextButton:Apply");

    clickButton(fadeOutButton);
    clickButton(applyButton);
    drainPendingEffectWork(&state);

    REQUIRE(state.getActiveUndoables().size() == 1);
    REQUIRE(state.modalWindow == nullptr);
    const bool dialogClosed = state.amplifyFadeDialog == nullptr ||
                              !state.amplifyFadeDialog->isOpen();
    REQUIRE(dialogClosed);
    REQUIRE(doc.getSample(0, 2) == Approx(1.0f));
    REQUIRE(doc.getSample(0, 5) == Approx(0.0f));
    REQUIRE(doc.getSample(1, 2) == Approx(1.0f));
    REQUIRE(doc.getSample(1, 5) == Approx(1.0f));
}

TEST_CASE(
    "AmplifyFade dialog cancel closes without applying and reopens with "
    "remembered settings",
    "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    auto ui = cupuacu::test::integration::createSessionUi(&state, 16);

    auto harness = createEffectsMenuHarness(&state);
    REQUIRE(harness.amplifyFadeMenu->mouseDown(
        cupuacu::test::integration::leftMouseDown()));

    auto *root = state.amplifyFadeDialog->getWindow()->getRootComponent();
    auto *fadeInButton = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::TextButton>(root, "TextButton:Fade in");
    auto *cancelButton = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::TextButton>(root, "TextButton:Cancel");

    clickButton(fadeInButton);
    clickButton(cancelButton);

    REQUIRE(state.getActiveUndoables().empty());
    REQUIRE(state.modalWindow == nullptr);
    REQUIRE_FALSE(state.amplifyFadeDialog->isOpen());
    REQUIRE(state.effectSettings.amplifyFade.startPercent == 0.0);
    REQUIRE(state.effectSettings.amplifyFade.endPercent == 100.0);

    state.amplifyFadeDialog.reset(nullptr);
    REQUIRE(harness.amplifyFadeMenu->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.amplifyFadeDialog->getStartPercent() == 0.0);
    REQUIRE(state.amplifyFadeDialog->getEndPercent() == 100.0);
}

TEST_CASE("AmplifyFade dialog treats Enter as Apply", "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    auto ui = cupuacu::test::integration::createSessionUi(&state, 8, false, 2);
    auto &session = state.getActiveDocumentSession();
    auto &doc = session.document;

    for (int64_t frame = 0; frame < doc.getFrameCount(); ++frame)
    {
        doc.setSample(0, frame, 1.0f, false);
        doc.setSample(1, frame, 1.0f, false);
    }

    session.selection.setValue1(2.0);
    session.selection.setValue2(6.0);
    state.getActiveViewState().selectedChannels =
        cupuacu::SelectedChannels::LEFT;

    auto harness = createEffectsMenuHarness(&state);
    REQUIRE(harness.amplifyFadeMenu->mouseDown(
        cupuacu::test::integration::leftMouseDown()));

    auto *root = state.amplifyFadeDialog->getWindow()->getRootComponent();
    auto *fadeOutButton = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::TextButton>(root, "TextButton:Fade out");
    clickButton(fadeOutButton);

    sendKeyDown(state.amplifyFadeDialog->getWindow(), SDL_SCANCODE_RETURN);
    processPendingSdlWindowEvents(&state);
    drainPendingEffectWork(&state);

    REQUIRE(state.getActiveUndoables().size() == 1);
    REQUIRE(state.modalWindow == nullptr);
    const bool dialogClosed = state.amplifyFadeDialog == nullptr ||
                              !state.amplifyFadeDialog->isOpen();
    REQUIRE(dialogClosed);
    REQUIRE(doc.getSample(0, 2) == Approx(1.0f));
    REQUIRE(doc.getSample(0, 5) == Approx(0.0f));
    REQUIRE(doc.getSample(1, 2) == Approx(1.0f));
    REQUIRE(doc.getSample(1, 5) == Approx(1.0f));
}

TEST_CASE(
    "Dynamics dialog preview uses the whole document and restarts from frame "
    "zero",
    "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    auto ui = cupuacu::test::integration::createSessionUi(&state, 32, true);
    state.getActiveDocumentSession().selection.reset();

    auto harness = createEffectsMenuHarness(&state);
    REQUIRE(harness.dynamicsMenu->mouseDown(
        cupuacu::test::integration::leftMouseDown()));

    auto *previewButton = cupuacu::test::integration::findByNameRecursive<
        cupuacu::gui::TextButton>(
        state.dynamicsDialog->getWindow()->getRootComponent(),
        "TextButton:Preview");

    clickButton(previewButton);
    state.audioDevices->drainQueue();

    REQUIRE(state.audioDevices->isPlaying());
    REQUIRE(state.playbackRangeStart == 0);
    REQUIRE(state.playbackRangeEnd == 32);
    REQUIRE(state.audioDevices->getPlaybackPosition() == 0);

    std::vector<float> output(6, 0.0f);
    state.audioDevices->processCallbackCycle(nullptr, output.data(), 3);
    REQUIRE(state.audioDevices->getPlaybackPosition() == 3);

    clickButton(previewButton);
    state.audioDevices->drainQueue();
    REQUIRE_FALSE(state.audioDevices->isPlaying());

    clickButton(previewButton);
    state.audioDevices->drainQueue();
    REQUIRE(state.audioDevices->isPlaying());
    REQUIRE(state.audioDevices->getPlaybackPosition() == 0);
}
