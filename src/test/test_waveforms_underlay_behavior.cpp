#include <catch2/catch_test_macros.hpp>

#include "TestStateBuilders.hpp"
#include "gui/Waveforms.hpp"
#include "gui/WaveformsUnderlay.hpp"

namespace
{
    cupuacu::gui::WaveformsUnderlay *findUnderlay(cupuacu::gui::MainView *mainView)
    {
        for (const auto &child : mainView->getChildren())
        {
            if (auto *waveforms =
                    dynamic_cast<cupuacu::gui::Waveforms *>(child.get()))
            {
                for (const auto &grandchild : waveforms->getChildren())
                {
                    if (auto *underlay =
                            dynamic_cast<cupuacu::gui::WaveformsUnderlay *>(
                                grandchild.get()))
                    {
                        return underlay;
                    }
                }
            }
        }
        return nullptr;
    }
} // namespace

TEST_CASE("WaveformsUnderlay single click starts selection at hovered sample",
          "[gui]")
{
    cupuacu::State state{};
    auto sessionUi = cupuacu::test::createSessionUi(&state, 128, false, 2);
    auto *underlay = findUnderlay(sessionUi.mainView.get());
    REQUIRE(underlay != nullptr);

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 1.0;
    viewState.sampleOffset = 10;

    REQUIRE(underlay->mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        7,
        10,
        7.0f,
        10.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));

    REQUIRE_FALSE(state.activeDocumentSession.selection.isActive());
    REQUIRE(state.activeDocumentSession.selection.getStartInt() == 17);
    REQUIRE(state.activeDocumentSession.cursor == 17);
}

TEST_CASE("WaveformsUnderlay double click selects visible range", "[gui]")
{
    cupuacu::State state{};
    auto sessionUi = cupuacu::test::createSessionUi(&state, 200, false, 2);
    auto *underlay = findUnderlay(sessionUi.mainView.get());
    REQUIRE(underlay != nullptr);

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 1.0;
    viewState.sampleOffset = 25;
    underlay->setBounds(0, 0, 50, 80);

    REQUIRE(underlay->mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        5,
        10,
        5.0f,
        10.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        2}));

    REQUIRE(state.activeDocumentSession.selection.isActive());
    REQUIRE(state.activeDocumentSession.selection.getStartInt() == 25);
    REQUIRE(state.activeDocumentSession.selection.getEndInt() == 74);
}

TEST_CASE("WaveformsUnderlay drag extends selection and updates channel hover",
          "[gui]")
{
    cupuacu::State state{};
    auto sessionUi = cupuacu::test::createSessionUi(&state, 100, false, 2);
    auto *underlay = findUnderlay(sessionUi.mainView.get());
    REQUIRE(underlay != nullptr);

    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 1.0;
    viewState.sampleOffset = 0;
    underlay->setBounds(0, 0, 60, 80);
    state.mainDocumentSessionWindow->getWindow()->setCapturingComponent(underlay);

    REQUIRE(underlay->mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        10,
        5,
        10.0f,
        5.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));

    REQUIRE(underlay->mouseMove(cupuacu::gui::MouseEvent{
        cupuacu::gui::MOVE,
        20,
        79,
        20.0f,
        79.0f,
        10.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));

    REQUIRE(state.activeDocumentSession.selection.isActive());
    REQUIRE(state.activeDocumentSession.selection.getStartInt() == 10);
    REQUIRE(state.activeDocumentSession.selection.getEndInt() == 19);
    REQUIRE(viewState.hoveringOverChannels == cupuacu::RIGHT);
    REQUIRE(viewState.selectedChannels == cupuacu::RIGHT);
}
