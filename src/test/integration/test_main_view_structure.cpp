#include <catch2/catch_test_macros.hpp>

#include "IntegrationTestHelpers.hpp"

#include "State.hpp"
#include "gui/Gui.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/MainView.hpp"
#include "gui/MenuBar.hpp"
#include "gui/OpaqueRect.hpp"
#include "gui/ScrollBar.hpp"
#include "gui/StatusBar.hpp"
#include "gui/TabStrip.hpp"
#include "gui/Timeline.hpp"
#include "gui/WaveformsUnderlay.hpp"
#include "gui/Waveforms.hpp"

#include <vector>

namespace
{
    std::vector<cupuacu::gui::Component *> componentChildren(
        cupuacu::gui::Component *parent)
    {
        std::vector<cupuacu::gui::Component *> result;
        for (const auto &child : parent->getChildren())
        {
            result.push_back(child.get());
        }
        return result;
    }
} // namespace

TEST_CASE("MainView integration contains expected coarse structure",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    auto sessionUi = cupuacu::test::integration::createSessionUi(&state, 128);
    auto *mainView = sessionUi.mainView;

    auto children = componentChildren(mainView);
    int borderCount = 0;
    int waveformsCount = 0;
    int timelineCount = 0;
    int scrollBarCount = 0;

    for (auto *child : children)
    {
        if (dynamic_cast<cupuacu::gui::Waveforms *>(child) != nullptr)
        {
            ++waveformsCount;
            continue;
        }
        if (dynamic_cast<cupuacu::gui::Timeline *>(child) != nullptr)
        {
            ++timelineCount;
            continue;
        }

        auto *border = dynamic_cast<cupuacu::gui::OpaqueRect *>(child);
        REQUIRE(border != nullptr);
        ++borderCount;

        for (const auto &grandchild : border->getChildren())
        {
            if (dynamic_cast<cupuacu::gui::ScrollBar *>(grandchild.get()) != nullptr)
            {
                ++scrollBarCount;
            }
        }
    }

    REQUIRE(borderCount == 4);
    REQUIRE(waveformsCount == 1);
    REQUIRE(timelineCount == 1);
    REQUIRE(scrollBarCount == 1);
}

TEST_CASE("Main window integration keeps the tab strip below overlay menus",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    cupuacu::test::ensureSdlTtfInitialized();

    state.mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            &state, &state.getActiveDocumentSession(), &state.getActiveViewState(),
            "structure", 800, 400, SDL_WINDOW_HIDDEN);
    cupuacu::gui::buildComponents(&state,
                                  state.mainDocumentSessionWindow->getWindow());

    auto *window = state.mainDocumentSessionWindow->getWindow();
    REQUIRE(window != nullptr);
    REQUIRE(window->getContentLayer() != nullptr);
    REQUIRE(window->getOverlayLayer() != nullptr);

    auto *tabStrip =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::TabStrip>(
            window->getContentLayer(), "TabStrip");
    REQUIRE(tabStrip != nullptr);

    auto *menuBar =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::MenuBar>(
            window->getOverlayLayer(), "MenuBar");
    REQUIRE(menuBar != nullptr);

    const auto contentChildren = componentChildren(window->getContentLayer());
    const auto overlayChildren = componentChildren(window->getOverlayLayer());
    REQUIRE(std::find(contentChildren.begin(), contentChildren.end(), tabStrip) !=
            contentChildren.end());
    REQUIRE(std::find(overlayChildren.begin(), overlayChildren.end(), menuBar) !=
            overlayChildren.end());
    REQUIRE(tabStrip->getHeight() == menuBar->getHeight());

    const auto topLevelMenus = cupuacu::test::integration::menuChildren(menuBar);
    REQUIRE_FALSE(topLevelMenus.empty());
    REQUIRE(topLevelMenus.front()->getHeight() == menuBar->getHeight());

    auto *statusBar =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::StatusBar>(
            window->getContentLayer(), "StatusBar");
    REQUIRE(statusBar != nullptr);
    REQUIRE(statusBar->getHeight() == menuBar->getHeight());
}

TEST_CASE("MainView integration double click selects the visible range",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    auto sessionUi = cupuacu::test::integration::createSessionUi(&state, 128);

    auto *underlay =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::WaveformsUnderlay>(
            sessionUi.mainView, "WaveformsUnderlay");
    REQUIRE(underlay != nullptr);

    REQUIRE(underlay->mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        10,
        10,
        10.0f,
        10.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        2}));

    const auto &selection = state.getActiveDocumentSession().selection;
    REQUIRE(selection.isActive());
    REQUIRE(selection.getStartInt() == 0);
    REQUIRE(selection.getEndInt() >=
            state.getActiveDocumentSession().document.getFrameCount() - 1);
}

TEST_CASE("MainView integration drag selection on the lower waveform selects the right channel",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    auto sessionUi = cupuacu::test::integration::createSessionUi(&state, 256);

    auto *underlay =
        cupuacu::test::integration::findByNameRecursive<cupuacu::gui::WaveformsUnderlay>(
            sessionUi.mainView, "WaveformsUnderlay");
    REQUIRE(underlay != nullptr);

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
    REQUIRE(underlay->mouseUp(cupuacu::gui::MouseEvent{
        cupuacu::gui::UP,
        120,
        dragY,
        120.0f,
        static_cast<float>(dragY),
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));

    const auto &selection = state.getActiveDocumentSession().selection;
    REQUIRE(selection.isActive());
    REQUIRE(selection.getLengthInt() > 0);
    REQUIRE(state.getActiveDocumentSession().cursor == selection.getStartInt());
}
