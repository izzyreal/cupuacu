#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "IntegrationTestHelpers.hpp"

#include "effects/AmplifyFadeEffect.hpp"
#include "effects/DynamicsEffect.hpp"
#include "State.hpp"
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

TEST_CASE("Effects menu integration opens AmplifyFade and Dynamics dialogs",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    state.mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            &state, &state.activeDocumentSession, "main", 640, 360,
            SDL_WINDOW_HIDDEN);
    state.windows.push_back(state.mainDocumentSessionWindow->getWindow());

    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "menu-bar-effects", 480, 240, SDL_WINDOW_HIDDEN);

    auto root = std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = cupuacu::test::integration::menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 5);
    auto *effectsMenu = topLevelMenus[3];
    auto effectSubMenus = cupuacu::test::integration::menuChildren(effectsMenu);
    REQUIRE(effectSubMenus.size() == 2);

    REQUIRE(state.amplifyFadeDialog == nullptr);
    REQUIRE(effectSubMenus[0]->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.amplifyFadeDialog != nullptr);
    REQUIRE(state.amplifyFadeDialog->isOpen());
    REQUIRE(state.modalWindow == state.amplifyFadeDialog->getWindow());

    REQUIRE(state.dynamicsDialog == nullptr);
    REQUIRE(effectSubMenus[1]->mouseDown(
        cupuacu::test::integration::leftMouseDown()));
    REQUIRE(state.dynamicsDialog != nullptr);
    REQUIRE(state.dynamicsDialog->isOpen());
}

TEST_CASE("AmplifyFade dialog integration exposes shared controls and presets",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    state.mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            &state, &state.activeDocumentSession, "main", 640, 360,
            SDL_WINDOW_HIDDEN);
    state.windows.push_back(state.mainDocumentSessionWindow->getWindow());
    state.amplifyFadeDialog.reset(
        new cupuacu::effects::AmplifyFadeDialog(&state));

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
