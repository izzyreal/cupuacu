#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "IntegrationTestHelpers.hpp"
#include "../TestPaths.hpp"

#include "effects/AmplifyFadeEffect.hpp"
#include "effects/DynamicsEffect.hpp"
#include "effects/RemoveSilenceEffect.hpp"
#include "effects/ReverseEffect.hpp"
#include "State.hpp"
#include "SelectedChannels.hpp"
#include "audio/AudioDevices.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/DocumentSessionWindow.hpp"
#include "gui/DropdownMenu.hpp"
#include "gui/EventHandling.hpp"
#include "gui/Label.hpp"
#include "gui/Menu.hpp"
#include "gui/MenuBar.hpp"
#include "gui/Slider.hpp"
#include "gui/TextButton.hpp"
#include "gui/Window.hpp"

#include <memory>

using Catch::Approx;

namespace
{
    struct EffectsMenuHarness
    {
        std::unique_ptr<cupuacu::gui::Window> window;
        cupuacu::gui::MenuBar *menuBar = nullptr;
        cupuacu::gui::Menu *reverseMenu = nullptr;
        cupuacu::gui::Menu *amplifyFadeMenu = nullptr;
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
                    state, &state->getActiveDocumentSession(), &state->getActiveViewState(), "main", 640, 360,
                    SDL_WINDOW_HIDDEN);
            state->windows.push_back(state->mainDocumentSessionWindow->getWindow());
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
        REQUIRE(effectSubMenus.size() == 4);
        harness.reverseMenu = effectSubMenus[0];
        harness.amplifyFadeMenu = effectSubMenus[1];
        harness.dynamicsMenu = effectSubMenus[2];
        harness.removeSilenceMenu = effectSubMenus[3];
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

    SDL_Event makeMouseButtonEvent(const Uint32 type,
                                   cupuacu::gui::Window *window, const float x,
                                   const float y, const bool leftDown)
    {
        SDL_Event event{};
        event.type = type;
        event.button.windowID = window != nullptr ? window->getId() : 0;
        event.button.x = x;
        event.button.y = y;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = leftDown;
        event.button.clicks = 1;
        return event;
    }

    void initializeActiveDocument(cupuacu::State *state)
    {
        state->getActiveDocumentSession().document.initialize(
            cupuacu::SampleFormat::FLOAT32, 44100, 1, 3);
    }
} // namespace

TEST_CASE("Effects menu integration opens AmplifyFade Dynamics and Remove silence dialogs",
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
        if (auto *dropdown = dynamic_cast<cupuacu::gui::DropdownMenu *>(child.get()))
        {
            dropdowns.push_back(dropdown);
        }
    }

    REQUIRE(dropdowns.size() >= 2);
    auto *modeDropdown = dropdowns[0];
    REQUIRE(modeDropdown->getSelectedIndex() == 0);

    const SDL_Rect bounds = modeDropdown->getBounds();
    SDL_Event down = makeMouseButtonEvent(
        SDL_EVENT_MOUSE_BUTTON_DOWN, dialogWindow, bounds.x + 12.0f,
        bounds.y + bounds.h * 0.5f, true);
    SDL_Event up = makeMouseButtonEvent(
        SDL_EVENT_MOUSE_BUTTON_UP, dialogWindow, bounds.x + 12.0f,
        bounds.y + bounds.h * 0.5f, true);
    REQUIRE(cupuacu::gui::handleAppEvent(&state, &down) == SDL_APP_CONTINUE);
    REQUIRE(cupuacu::gui::handleAppEvent(&state, &up) == SDL_APP_CONTINUE);
    REQUIRE(modeDropdown->isExpanded());

    cupuacu::gui::Window *popupWindow = nullptr;
    for (auto *candidate : state.windows)
    {
        if (candidate != dialogWindow && candidate != state.mainDocumentSessionWindow->getWindow())
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

TEST_CASE("Reverse menu integration applies immediately without opening a dialog",
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

    auto *previewButton =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::TextButton>(
            root, "TextButton:Preview");
    auto *curveLabel =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::Label>(
            root, "Label: Curve");
    auto *curveDropdown =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::DropdownMenu>(
            root, "DropdownMenu");
    auto *resetButton =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::TextButton>(
            root, "TextButton:Reset");
    auto *lockButton =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::TextButton>(
            root, "TextButton:Lock");
    auto *normalizeButton =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::TextButton>(
            root, "TextButton:Normalize");
    auto *fadeInButton =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::TextButton>(
            root, "TextButton:Fade in");
    auto *fadeOutButton =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::TextButton>(
            root, "TextButton:Fade out");

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

    REQUIRE(fadeOutButton->mouseDown(cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.amplifyFadeDialog->getStartPercent() == 100.0);
    REQUIRE(state.amplifyFadeDialog->getEndPercent() == 0.0);

    REQUIRE(fadeInButton->mouseDown(cupuacu::test::integration::leftMouseDown()));
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

    REQUIRE(resetButton->mouseDown(cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.amplifyFadeDialog->getStartPercent() == 100.0);
    REQUIRE(state.amplifyFadeDialog->getEndPercent() == 100.0);
}

TEST_CASE("AmplifyFade dialog preview toggles playback and restarts from the selection start",
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

    auto *previewButton =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::TextButton>(
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

TEST_CASE("AmplifyFade dialog apply closes and respects the selected channel target",
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
    auto *fadeOutButton =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::TextButton>(
            root, "TextButton:Fade out");
    auto *applyButton =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::TextButton>(
            root, "TextButton:Apply");

    clickButton(fadeOutButton);
    clickButton(applyButton);

    REQUIRE(state.getActiveUndoables().size() == 1);
    REQUIRE(state.modalWindow == nullptr);
    const bool dialogClosed =
        state.amplifyFadeDialog == nullptr || !state.amplifyFadeDialog->isOpen();
    REQUIRE(dialogClosed);
    REQUIRE(doc.getSample(0, 2) == Approx(1.0f));
    REQUIRE(doc.getSample(0, 5) == Approx(0.0f));
    REQUIRE(doc.getSample(1, 2) == Approx(1.0f));
    REQUIRE(doc.getSample(1, 5) == Approx(1.0f));
}

TEST_CASE("AmplifyFade dialog cancel closes without applying and reopens with remembered settings",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    auto ui = cupuacu::test::integration::createSessionUi(&state, 16);

    auto harness = createEffectsMenuHarness(&state);
    REQUIRE(harness.amplifyFadeMenu->mouseDown(
        cupuacu::test::integration::leftMouseDown()));

    auto *root = state.amplifyFadeDialog->getWindow()->getRootComponent();
    auto *fadeInButton =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::TextButton>(
            root, "TextButton:Fade in");
    auto *cancelButton =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::TextButton>(
            root, "TextButton:Cancel");

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
    auto *fadeOutButton =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::TextButton>(
            root, "TextButton:Fade out");
    clickButton(fadeOutButton);

    sendKeyDown(state.amplifyFadeDialog->getWindow(), SDL_SCANCODE_RETURN);
    processPendingSdlWindowEvents(&state);

    REQUIRE(state.getActiveUndoables().size() == 1);
    REQUIRE(state.modalWindow == nullptr);
    const bool dialogClosed =
        state.amplifyFadeDialog == nullptr || !state.amplifyFadeDialog->isOpen();
    REQUIRE(dialogClosed);
    REQUIRE(doc.getSample(0, 2) == Approx(1.0f));
    REQUIRE(doc.getSample(0, 5) == Approx(0.0f));
    REQUIRE(doc.getSample(1, 2) == Approx(1.0f));
    REQUIRE(doc.getSample(1, 5) == Approx(1.0f));
}

TEST_CASE("Dynamics dialog preview uses the whole document and restarts from frame zero",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    auto ui = cupuacu::test::integration::createSessionUi(&state, 32, true);
    state.getActiveDocumentSession().selection.reset();

    auto harness = createEffectsMenuHarness(&state);
    REQUIRE(harness.dynamicsMenu->mouseDown(
        cupuacu::test::integration::leftMouseDown()));

    auto *previewButton =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::TextButton>(
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
