#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestSdlTtfGuard.hpp"
#include "actions/Undoable.hpp"
#include "gui/Component.hpp"
#include "gui/AmplifyFadeWindow.hpp"
#include "gui/DynamicsWindow.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/DocumentSessionWindow.hpp"
#include "gui/Label.hpp"
#include "gui/Menu.hpp"
#include "gui/MenuBar.hpp"
#include "gui/MenuBarPlanning.hpp"
#include "gui/NormalizeWindow.hpp"
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

    class TestUndoable : public cupuacu::actions::Undoable
    {
    public:
        TestUndoable(cupuacu::State *state, std::string description)
            : Undoable(state), description(std::move(description))
        {
        }

        std::string description;

        void redo() override {}
        void undo() override {}

        std::string getRedoDescription() override
        {
            return description;
        }

        std::string getUndoDescription() override
        {
            return description;
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

    cupuacu::gui::Label *findMenuLabel(cupuacu::gui::Menu *menu)
    {
        return dynamic_cast<cupuacu::gui::Label *>(menu->getChildren().front().get());
    }
} // namespace

TEST_CASE("MenuBar planning builds dynamic undo redo labels and availability",
          "[gui]")
{
    cupuacu::State state{};

#ifdef __APPLE__
    REQUIRE(cupuacu::gui::buildUndoMenuLabel(&state) == "Undo (Cmd + Z)");
    REQUIRE(cupuacu::gui::buildRedoMenuLabel(&state) ==
            "Redo (Cmd + Shift + Z)");
#else
    REQUIRE(cupuacu::gui::buildUndoMenuLabel(&state) == "Undo (Ctrl + Z)");
    REQUIRE(cupuacu::gui::buildRedoMenuLabel(&state) ==
            "Redo (Ctrl + Shift + Z)");
#endif

    REQUIRE_FALSE(cupuacu::gui::isSelectionEditAvailable(&state));
    REQUIRE_FALSE(cupuacu::gui::isPasteAvailable(&state));

    state.activeDocumentSession.document.initialize(cupuacu::SampleFormat::FLOAT32,
                                                    44100, 1, 4);
    state.activeDocumentSession.selection.setHighest(4.0);
    state.activeDocumentSession.selection.setValue1(1.0);
    state.activeDocumentSession.selection.setValue2(3.0);
    state.clipboard.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, 2);

    REQUIRE(cupuacu::gui::isSelectionEditAvailable(&state));
    REQUIRE(cupuacu::gui::isPasteAvailable(&state));

    auto undoable = std::make_shared<TestUndoable>(&state, "Sample edit");
    state.addUndoable(undoable);

#ifdef __APPLE__
    REQUIRE(cupuacu::gui::buildUndoMenuLabel(&state) ==
            "Undo Sample edit (Cmd + Z)");
#else
    REQUIRE(cupuacu::gui::buildUndoMenuLabel(&state) ==
            "Undo Sample edit (Ctrl + Z)");
#endif

    state.undo();

#ifdef __APPLE__
    REQUIRE(cupuacu::gui::buildRedoMenuLabel(&state) ==
            "Redo Sample edit (Cmd + Shift + Z)");
#else
    REQUIRE(cupuacu::gui::buildRedoMenuLabel(&state) ==
            "Redo Sample edit (Ctrl + Shift + Z)");
#endif
}

TEST_CASE("MenuBar integration reflects dynamic undo label text", "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "menu-bar-planning", 480, 240, SDL_WINDOW_HIDDEN);

    auto root = std::make_unique<RootComponent>(&state);
    auto *menuBar = root->emplaceChild<cupuacu::gui::MenuBar>(&state);
    root->setBounds(0, 0, 480, 240);
    menuBar->setBounds(0, 0, 480, 40);
    window->setRootComponent(std::move(root));
    window->setMenuBar(menuBar);

    auto topLevelMenus = menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 5);
    for (auto *menu : topLevelMenus)
    {
        menu->onDraw(window->getRenderer());
    }
    REQUIRE(findMenuLabel(topLevelMenus[0])->getText() == "File");
    REQUIRE(findMenuLabel(topLevelMenus[1])->getText() == "Edit");
    REQUIRE(findMenuLabel(topLevelMenus[2])->getText() == "View");
    REQUIRE(findMenuLabel(topLevelMenus[3])->getText() == "Effects");
    REQUIRE(findMenuLabel(topLevelMenus[4])->getText() == "Options");

    auto *editMenu = topLevelMenus[1];
    auto editSubMenus = menuChildren(editMenu);
    REQUIRE(editSubMenus.size() == 6);
    auto *undoMenu = editSubMenus[0];

    undoMenu->onDraw(window->getRenderer());
    auto *label = findMenuLabel(undoMenu);
    REQUIRE(label != nullptr);
#ifdef __APPLE__
    REQUIRE(label->getText() == "Undo (Cmd + Z)");
#else
    REQUIRE(label->getText() == "Undo (Ctrl + Z)");
#endif

    auto undoable = std::make_shared<TestUndoable>(&state, "Cut");
    state.addUndoable(undoable);
    undoMenu->onDraw(window->getRenderer());
#ifdef __APPLE__
    REQUIRE(label->getText() == "Undo Cut (Cmd + Z)");
#else
    REQUIRE(label->getText() == "Undo Cut (Ctrl + Z)");
#endif
}

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
    REQUIRE(effectSubMenus.size() == 3);

    REQUIRE(state.amplifyFadeWindow == nullptr);
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

    REQUIRE(state.amplifyFadeWindow != nullptr);
    REQUIRE(state.amplifyFadeWindow->isOpen());

    REQUIRE(state.normalizeWindow == nullptr);
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
    REQUIRE(state.normalizeWindow != nullptr);
    REQUIRE(state.normalizeWindow->isOpen());

    REQUIRE(state.dynamicsWindow == nullptr);
    REQUIRE(effectSubMenus[2]->mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        5,
        5,
        5.0f,
        5.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));
    REQUIRE(state.dynamicsWindow != nullptr);
    REQUIRE(state.dynamicsWindow->isOpen());
}

TEST_CASE("AmplifyFadeWindow presets update values and curve row does not overlap",
          "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    state.mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            &state, &state.activeDocumentSession, "main", 640, 360,
            SDL_WINDOW_HIDDEN);
    state.windows.push_back(state.mainDocumentSessionWindow->getWindow());

    auto amplifyFadeWindow =
        std::make_unique<cupuacu::gui::AmplifyFadeWindow>(&state);
    REQUIRE(amplifyFadeWindow->isOpen());

    auto *root = amplifyFadeWindow->getWindow()->getRootComponent();
    REQUIRE(root != nullptr);

    auto *curveLabel = dynamic_cast<cupuacu::gui::Label *>(
        const_cast<cupuacu::gui::Component *>(
            findByNameRecursive(root, "Label: Curve")));
    auto *curveDropdown = dynamic_cast<cupuacu::gui::DropdownMenu *>(
        const_cast<cupuacu::gui::Component *>(
            findByNameRecursive(root, "DropdownMenu")));
    auto *resetButton = dynamic_cast<cupuacu::gui::TextButton *>(
        const_cast<cupuacu::gui::Component *>(
            findByNameRecursive(root, "TextButton:Reset")));
    auto *fadeInButton = dynamic_cast<cupuacu::gui::TextButton *>(
        const_cast<cupuacu::gui::Component *>(
            findByNameRecursive(root, "TextButton:Fade in")));
    auto *fadeOutButton = dynamic_cast<cupuacu::gui::TextButton *>(
        const_cast<cupuacu::gui::Component *>(
            findByNameRecursive(root, "TextButton:Fade out")));

    REQUIRE(curveLabel != nullptr);
    REQUIRE(curveDropdown != nullptr);
    REQUIRE(resetButton != nullptr);
    REQUIRE(fadeInButton != nullptr);
    REQUIRE(fadeOutButton != nullptr);

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
    REQUIRE(amplifyFadeWindow->getStartPercent() == 100.0);
    REQUIRE(amplifyFadeWindow->getEndPercent() == 0.0);

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
    REQUIRE(amplifyFadeWindow->getStartPercent() == 0.0);
    REQUIRE(amplifyFadeWindow->getEndPercent() == 100.0);

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
    REQUIRE(amplifyFadeWindow->getStartPercent() == 100.0);
    REQUIRE(amplifyFadeWindow->getEndPercent() == 100.0);
}

TEST_CASE("AmplifyFadeWindow lock and apply respect selected channels",
          "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    state.activeDocumentSession.document.initialize(cupuacu::SampleFormat::FLOAT32,
                                                    44100, 2, 4);
    for (int64_t i = 0; i < 4; ++i)
    {
        state.activeDocumentSession.document.setSample(0, i, float(i + 1), false);
        state.activeDocumentSession.document.setSample(
            1, i, float((i + 1) * 10), false);
    }
    state.activeDocumentSession.selection.setHighest(4.0);
    state.activeDocumentSession.selection.setValue1(1.0);
    state.activeDocumentSession.selection.setValue2(3.0);

    state.mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            &state, &state.activeDocumentSession, "main", 640, 360,
            SDL_WINDOW_HIDDEN);
    state.mainDocumentSessionWindow->getViewState().selectedChannels =
        cupuacu::SelectedChannels::RIGHT;
    state.windows.push_back(state.mainDocumentSessionWindow->getWindow());

    auto amplifyFadeWindow =
        std::make_unique<cupuacu::gui::AmplifyFadeWindow>(&state);
    REQUIRE(amplifyFadeWindow->isOpen());

    auto *root = amplifyFadeWindow->getWindow()->getRootComponent();
    REQUIRE(root != nullptr);

    cupuacu::gui::TextButton *lockButton = nullptr;
    cupuacu::gui::TextButton *cancelButton = nullptr;
    cupuacu::gui::TextButton *applyButton = nullptr;
    std::vector<cupuacu::gui::Slider *> sliders;
    for (const auto &child : root->getChildren())
    {
        if (auto *button = dynamic_cast<cupuacu::gui::TextButton *>(child.get()))
        {
            if (button->getComponentName() == "TextButton:Lock")
            {
                lockButton = button;
            }
            else if (button->getComponentName() == "TextButton:Cancel")
            {
                cancelButton = button;
            }
            else if (button->getComponentName() == "TextButton:Apply")
            {
                applyButton = button;
            }
        }
        if (auto *slider = dynamic_cast<cupuacu::gui::Slider *>(child.get()))
        {
            sliders.push_back(slider);
        }
    }

    REQUIRE(lockButton != nullptr);
    REQUIRE(cancelButton != nullptr);
    REQUIRE(applyButton != nullptr);
    REQUIRE(sliders.size() == 2);

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
    REQUIRE(amplifyFadeWindow->isLocked());
    REQUIRE(amplifyFadeWindow->getStartPercent() == amplifyFadeWindow->getEndPercent());

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
    REQUIRE(amplifyFadeWindow->getStartPercent() == amplifyFadeWindow->getEndPercent());

    const float gain =
        static_cast<float>(amplifyFadeWindow->getStartPercent() / 100.0);
    const SDL_Rect applyBounds = applyButton->getBounds();
    REQUIRE(amplifyFadeWindow->getWindow()->handleMouseEvent(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        applyBounds.x + 5,
        applyBounds.y + 5,
        static_cast<float>(applyBounds.x + 5),
        static_cast<float>(applyBounds.y + 5),
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));

    REQUIRE(state.activeDocumentSession.document.getSample(0, 0) == 1.0f);
    REQUIRE(state.activeDocumentSession.document.getSample(0, 1) == 2.0f);
    REQUIRE(state.activeDocumentSession.document.getSample(0, 2) == 3.0f);
    REQUIRE(state.activeDocumentSession.document.getSample(1, 0) == 10.0f);
    REQUIRE(state.activeDocumentSession.document.getSample(1, 1) ==
            Catch::Approx(20.0f * gain));
    REQUIRE(state.activeDocumentSession.document.getSample(1, 2) ==
            Catch::Approx(30.0f * gain));
    REQUIRE(state.activeDocumentSession.document.getSample(1, 3) == 40.0f);

    REQUIRE_FALSE(amplifyFadeWindow->isOpen());
}

TEST_CASE("AmplifyFadeWindow reopens with last remembered settings", "[gui]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    state.mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            &state, &state.activeDocumentSession, "main", 640, 360,
            SDL_WINDOW_HIDDEN);
    state.windows.push_back(state.mainDocumentSessionWindow->getWindow());

    {
        auto amplifyFadeWindow =
            std::make_unique<cupuacu::gui::AmplifyFadeWindow>(&state);
        REQUIRE(amplifyFadeWindow->isOpen());

        auto *root = amplifyFadeWindow->getWindow()->getRootComponent();
        REQUIRE(root != nullptr);

        auto *fadeOutButton = dynamic_cast<cupuacu::gui::TextButton *>(
            const_cast<cupuacu::gui::Component *>(
                findByNameRecursive(root, "TextButton:Fade out")));
        auto *curveDropdown = dynamic_cast<cupuacu::gui::DropdownMenu *>(
            const_cast<cupuacu::gui::Component *>(
                findByNameRecursive(root, "DropdownMenu")));
        REQUIRE(fadeOutButton != nullptr);
        REQUIRE(curveDropdown != nullptr);

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

        amplifyFadeWindow.reset();
    }

    auto reopenedWindow =
        std::make_unique<cupuacu::gui::AmplifyFadeWindow>(&state);
    REQUIRE(reopenedWindow->isOpen());
    REQUIRE(reopenedWindow->getStartPercent() == 100.0);
    REQUIRE(reopenedWindow->getEndPercent() == 0.0);
    REQUIRE_FALSE(reopenedWindow->isLocked());
    REQUIRE(reopenedWindow->getCurveIndex() == 2);
}
