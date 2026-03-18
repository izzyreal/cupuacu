#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "IntegrationTestHelpers.hpp"

#include "effects/AmplifyFadeEffect.hpp"
#include "effects/DynamicsEffect.hpp"
#include "State.hpp"
#include "SelectedChannels.hpp"
#include "audio/AudioDevices.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/DocumentSessionWindow.hpp"
#include "gui/DropdownMenu.hpp"
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
        cupuacu::gui::Menu *amplifyFadeMenu = nullptr;
        cupuacu::gui::Menu *dynamicsMenu = nullptr;
    };

    EffectsMenuHarness createEffectsMenuHarness(cupuacu::State *state)
    {
        EffectsMenuHarness harness{};
        if (!state->mainDocumentSessionWindow)
        {
            state->mainDocumentSessionWindow =
                std::make_unique<cupuacu::gui::DocumentSessionWindow>(
                    state, &state->activeDocumentSession, "main", 640, 360,
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
        REQUIRE(topLevelMenus.size() == 5);
        auto *effectsMenu = topLevelMenus[3];
        auto effectSubMenus =
            cupuacu::test::integration::menuChildren(effectsMenu);
        REQUIRE(effectSubMenus.size() == 2);
        harness.amplifyFadeMenu = effectSubMenus[0];
        harness.dynamicsMenu = effectSubMenus[1];
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
} // namespace

TEST_CASE("Effects menu integration opens AmplifyFade and Dynamics dialogs",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
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
}

TEST_CASE("AmplifyFade dialog integration exposes shared controls and presets",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
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

    state.activeDocumentSession.document.initialize(
        cupuacu::SampleFormat::FLOAT32, 44100, 1, 3);
    state.activeDocumentSession.document.setSample(0, 0, 0.25f, false);
    state.activeDocumentSession.document.setSample(0, 1, -0.5f, false);
    state.activeDocumentSession.document.setSample(0, 2, 0.1f, false);
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
    cupuacu::State state{};
    auto ui = cupuacu::test::integration::createSessionUi(&state, 64, true);
    auto &session = state.activeDocumentSession;
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
    cupuacu::State state{};
    auto ui = cupuacu::test::integration::createSessionUi(&state, 8, false, 2);
    auto &session = state.activeDocumentSession;
    auto &doc = session.document;

    for (int64_t frame = 0; frame < doc.getFrameCount(); ++frame)
    {
        doc.setSample(0, frame, 1.0f, false);
        doc.setSample(1, frame, 1.0f, false);
    }

    session.selection.setValue1(2.0);
    session.selection.setValue2(6.0);
    state.mainDocumentSessionWindow->getViewState().selectedChannels =
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

    REQUIRE(state.undoables.size() == 1);
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
    cupuacu::State state{};
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

    REQUIRE(state.undoables.empty());
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

TEST_CASE("Dynamics dialog preview uses the whole document and restarts from frame zero",
          "[integration]")
{
    cupuacu::State state{};
    auto ui = cupuacu::test::integration::createSessionUi(&state, 32, true);
    state.activeDocumentSession.selection.reset();

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
