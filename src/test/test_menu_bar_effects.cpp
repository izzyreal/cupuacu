#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "effects/AmplifyFadeEffect.hpp"
#include "effects/DynamicsEffect.hpp"
#include "State.hpp"
#include "TestSdlTtfGuard.hpp"
#include "gui/Component.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/DocumentSessionWindow.hpp"
#include "gui/Label.hpp"
#include "gui/Menu.hpp"
#include "gui/MenuBar.hpp"
#include "gui/MenuBarPlanning.hpp"
#include "gui/TextButton.hpp"
#include "gui/DropdownMenu.hpp"
#include "gui/Slider.hpp"
#include "gui/Window.hpp"

#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    class RootComponent : public cupuacu::gui::Component
    {
    public:
        explicit RootComponent(cupuacu::State *state)
            : Component(state, "Root")
        {
        }
    };

    const cupuacu::gui::Component *
    findByNameRecursive(const cupuacu::gui::Component *root,
                        const std::string_view name)
    {
        if (root == nullptr)
        {
            return nullptr;
        }
        if (root->getComponentName() == name)
        {
            return root;
        }
        for (const auto &child : root->getChildren())
        {
            if (const auto *found = findByNameRecursive(child.get(), name))
            {
                return found;
            }
        }
        return nullptr;
    }

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
} // namespace

TEST_CASE("Effects menu opens AmplifyFadeWindow", "[gui]")
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

    auto root = std::make_unique<RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 5);
    auto *effectsMenu = topLevelMenus[3];
    auto effectSubMenus = menuChildren(effectsMenu);
    REQUIRE(effectSubMenus.size() == 2);

    REQUIRE(state.amplifyFadeDialog == nullptr);
    REQUIRE(effectSubMenus[0]->mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        5,
        5,
        5.0f,
        5.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));

    REQUIRE(state.amplifyFadeDialog != nullptr);
    REQUIRE(state.amplifyFadeDialog->isOpen());
    REQUIRE(state.modalWindow == state.amplifyFadeDialog->getWindow());
    {
        auto *root = state.amplifyFadeDialog->getWindow()->getRootComponent();
        REQUIRE(root != nullptr);
        auto *previewButton = dynamic_cast<cupuacu::gui::TextButton *>(
            const_cast<cupuacu::gui::Component *>(
                findByNameRecursive(root, "TextButton:Preview")));
        REQUIRE(previewButton != nullptr);

        auto *curveLabel = dynamic_cast<cupuacu::gui::Label *>(
            const_cast<cupuacu::gui::Component *>(
                findByNameRecursive(root, "Label: Curve")));
        auto *curveDropdown = dynamic_cast<cupuacu::gui::DropdownMenu *>(
            const_cast<cupuacu::gui::Component *>(
                findByNameRecursive(root, "DropdownMenu")));
        auto *resetButton = dynamic_cast<cupuacu::gui::TextButton *>(
            const_cast<cupuacu::gui::Component *>(
                findByNameRecursive(root, "TextButton:Reset")));
        auto *lockButton = dynamic_cast<cupuacu::gui::TextButton *>(
            const_cast<cupuacu::gui::Component *>(
                findByNameRecursive(root, "TextButton:Lock")));
        auto *normalizeButton = dynamic_cast<cupuacu::gui::TextButton *>(
            const_cast<cupuacu::gui::Component *>(
                findByNameRecursive(root, "TextButton:Normalize")));
        auto *fadeInButton = dynamic_cast<cupuacu::gui::TextButton *>(
            const_cast<cupuacu::gui::Component *>(
                findByNameRecursive(root, "TextButton:Fade in")));
        auto *fadeOutButton = dynamic_cast<cupuacu::gui::TextButton *>(
            const_cast<cupuacu::gui::Component *>(
                findByNameRecursive(root, "TextButton:Fade out")));
        std::vector<cupuacu::gui::Slider *> sliders;
        for (const auto &child : root->getChildren())
        {
            if (auto *slider =
                    dynamic_cast<cupuacu::gui::Slider *>(child.get()))
            {
                sliders.push_back(slider);
            }
        }

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

        REQUIRE(fadeOutButton->mouseDown(cupuacu::gui::MouseEvent{
            cupuacu::gui::DOWN,
            5,
            5,
            5.0f,
            5.0f,
            0.0f,
            0.0f,
            cupuacu::gui::MouseButtonState{true, false, false},
            1}));
        REQUIRE(state.amplifyFadeDialog->getStartPercent() == 100.0);
        REQUIRE(state.amplifyFadeDialog->getEndPercent() == 0.0);

        REQUIRE(fadeInButton->mouseDown(cupuacu::gui::MouseEvent{
            cupuacu::gui::DOWN,
            5,
            5,
            5.0f,
            5.0f,
            0.0f,
            0.0f,
            cupuacu::gui::MouseButtonState{true, false, false},
            1}));
        REQUIRE(state.amplifyFadeDialog->getStartPercent() == 0.0);
        REQUIRE(state.amplifyFadeDialog->getEndPercent() == 100.0);

        REQUIRE(lockButton->mouseDown(cupuacu::gui::MouseEvent{
            cupuacu::gui::DOWN,
            5,
            5,
            5.0f,
            5.0f,
            0.0f,
            0.0f,
            cupuacu::gui::MouseButtonState{true, false, false},
            1}));
        REQUIRE(state.amplifyFadeDialog->isLocked());
        REQUIRE(state.amplifyFadeDialog->getStartPercent() ==
                state.amplifyFadeDialog->getEndPercent());

        auto *startSlider = sliders.front();
        REQUIRE(startSlider->mouseDown(cupuacu::gui::MouseEvent{
            cupuacu::gui::DOWN,
            startSlider->getWidth() / 2,
            startSlider->getHeight() / 2,
            static_cast<float>(startSlider->getWidth() / 2),
            static_cast<float>(startSlider->getHeight() / 2),
            0.0f,
            0.0f,
            cupuacu::gui::MouseButtonState{true, false, false},
            1}));
        REQUIRE(state.amplifyFadeDialog->getStartPercent() ==
                state.amplifyFadeDialog->getEndPercent());

        state.activeDocumentSession.document.initialize(
            cupuacu::SampleFormat::FLOAT32, 44100, 1, 3);
        state.activeDocumentSession.document.setSample(0, 0, 0.25f, false);
        state.activeDocumentSession.document.setSample(0, 1, -0.5f, false);
        state.activeDocumentSession.document.setSample(0, 2, 0.1f, false);
        REQUIRE(normalizeButton->mouseDown(cupuacu::gui::MouseEvent{
            cupuacu::gui::DOWN,
            5,
            5,
            5.0f,
            5.0f,
            0.0f,
            0.0f,
            cupuacu::gui::MouseButtonState{true, false, false},
            1}));
        REQUIRE(state.amplifyFadeDialog->getStartPercent() ==
                Catch::Approx(200.0));
        REQUIRE(state.amplifyFadeDialog->getEndPercent() ==
                Catch::Approx(200.0));

        REQUIRE(resetButton->mouseDown(cupuacu::gui::MouseEvent{
            cupuacu::gui::DOWN,
            5,
            5,
            5.0f,
            5.0f,
            0.0f,
            0.0f,
            cupuacu::gui::MouseButtonState{true, false, false},
            1}));
        REQUIRE(state.amplifyFadeDialog->getStartPercent() == 100.0);
        REQUIRE(state.amplifyFadeDialog->getEndPercent() == 100.0);

        REQUIRE(lockButton->mouseDown(cupuacu::gui::MouseEvent{
            cupuacu::gui::DOWN,
            5,
            5,
            5.0f,
            5.0f,
            0.0f,
            0.0f,
            cupuacu::gui::MouseButtonState{true, false, false},
            1}));
        REQUIRE_FALSE(state.amplifyFadeDialog->isLocked());

        REQUIRE(fadeOutButton->mouseDown(cupuacu::gui::MouseEvent{
            cupuacu::gui::DOWN,
            5,
            5,
            5.0f,
            5.0f,
            0.0f,
            0.0f,
            cupuacu::gui::MouseButtonState{true, false, false},
            1}));
        curveDropdown->setSelectedIndex(2);
        state.effectSettings.amplifyFade.curveIndex = 2;
        state.effectSettings.amplifyFade.lockEnabled = false;

        state.amplifyFadeDialog.reset();
        state.modalWindow = nullptr;

        REQUIRE(effectSubMenus[0]->mouseDown(cupuacu::gui::MouseEvent{
            cupuacu::gui::DOWN,
            5,
            5,
            5.0f,
            5.0f,
            0.0f,
            0.0f,
            cupuacu::gui::MouseButtonState{true, false, false},
            1}));
        REQUIRE(state.amplifyFadeDialog != nullptr);
        REQUIRE(state.amplifyFadeDialog->isOpen());
        REQUIRE(state.amplifyFadeDialog->getStartPercent() == 100.0);
        REQUIRE(state.amplifyFadeDialog->getEndPercent() == 0.0);
        REQUIRE_FALSE(state.amplifyFadeDialog->isLocked());
        REQUIRE(state.amplifyFadeDialog->getCurveIndex() == 2);
    }

    REQUIRE(state.dynamicsDialog == nullptr);
    REQUIRE(effectSubMenus[1]->mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        5,
        5,
        5.0f,
        5.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));
    REQUIRE(state.dynamicsDialog != nullptr);
    REQUIRE(state.dynamicsDialog->isOpen());
}
