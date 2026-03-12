#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestStateBuilders.hpp"
#include "gui/Component.hpp"
#include "gui/SamplePoint.hpp"
#include "gui/TriangleMarker.hpp"
#include "gui/TriangleMarkerInteractionPlanning.hpp"
#include "gui/WaveformRefresh.hpp"

namespace
{
    cupuacu::gui::MouseEvent makeMouseEvent(
        const cupuacu::gui::MouseEventType type, const float mouseXf,
        const float mouseYf, const float mouseRelY,
        const bool left = false)
    {
        return {type,
                static_cast<int32_t>(mouseXf),
                static_cast<int32_t>(mouseYf),
                mouseXf,
                mouseYf,
                0.0f,
                mouseRelY,
                {left, false, false},
                1};
    }

    cupuacu::gui::Component *findByNameRecursive(cupuacu::gui::Component *root,
                                                 const std::string &name)
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
            if (auto *found = findByNameRecursive(child.get(), name))
            {
                return found;
            }
        }
        return nullptr;
    }
} // namespace

TEST_CASE("TriangleMarker planning derives drag anchors and collapsed selection behavior",
          "[gui]")
{
    REQUIRE(cupuacu::gui::planTriangleMarkerDragStartSample(
                cupuacu::gui::TriangleMarkerType::SelectionStartTop, 10.0, 20,
                5) == Catch::Approx(10.0));
    REQUIRE(cupuacu::gui::planTriangleMarkerDragStartSample(
                cupuacu::gui::TriangleMarkerType::SelectionEndBottom, 10.0, 20,
                5) == Catch::Approx(20.0));
    REQUIRE(cupuacu::gui::planTriangleMarkerDragStartSample(
                cupuacu::gui::TriangleMarkerType::CursorTop, 10.0, 20, 5) ==
            Catch::Approx(5.0));

    const auto downPlan = cupuacu::gui::planTriangleMarkerMouseDown(
        cupuacu::gui::TriangleMarkerType::SelectionStartTop, 10.0, 20, 5,
        4.0f, 2.0, true);
    REQUIRE(downPlan.dragStartSample == Catch::Approx(10.0));
    REQUIRE(downPlan.dragMouseOffsetParentX == Catch::Approx(-2.0f));
    REQUIRE(downPlan.shouldFixSelectionOrder);

    REQUIRE(cupuacu::gui::planTriangleMarkerDraggedSamplePosition(9.0f, 2.0,
                                                                  -2.0f) ==
            Catch::Approx(20.0));
    REQUIRE(cupuacu::gui::planTriangleMarkerSelectionValue(20.0, 20, true) ==
            21);
    REQUIRE(cupuacu::gui::planTriangleMarkerSelectionValue(19.0, 20, true) ==
            19);
}

TEST_CASE("SamplePoint drag updates sample value and records undoable", "[gui]")
{
    cupuacu::State state{};
    auto ui = cupuacu::test::createSessionUi(&state, 32, false, 2);
    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 0.01;

    state.activeDocumentSession.document.setSample(0, 0, 0.0f);
    cupuacu::gui::refreshWaveforms(&state, true, false);

    auto *waveform = state.waveforms[0];
    REQUIRE_FALSE(waveform->getChildren().empty());
    auto *samplePoint =
        dynamic_cast<cupuacu::gui::SamplePoint *>(waveform->getChildren().front().get());
    REQUIRE(samplePoint != nullptr);

    const auto rightDown =
        makeMouseEvent(cupuacu::gui::DOWN, 0.0f, 0.0f, 0.0f, false);
    REQUIRE_FALSE(samplePoint->mouseDown(rightDown));

    const auto leftDown =
        makeMouseEvent(cupuacu::gui::DOWN, 0.0f, 0.0f, 0.0f, true);
    REQUIRE(samplePoint->mouseDown(leftDown));

    const auto dragMove =
        makeMouseEvent(cupuacu::gui::MOVE, 0.0f, 0.0f, -500.0f, true);
    REQUIRE(samplePoint->mouseMove(dragMove));
    REQUIRE(state.activeDocumentSession.document.getSample(0, 0) ==
            Catch::Approx(1.0f));
    REQUIRE(viewState.sampleValueUnderMouseCursor.has_value());
    REQUIRE(*viewState.sampleValueUnderMouseCursor == Catch::Approx(1.0f));

    const auto mouseUp =
        makeMouseEvent(cupuacu::gui::UP, 0.0f, 0.0f, 0.0f, true);
    REQUIRE(samplePoint->mouseUp(mouseUp));
    REQUIRE(state.undoables.size() == 1);
    REQUIRE(state.getUndoDescription() == "Change sample value");

    state.undo();
    REQUIRE(state.activeDocumentSession.document.getSample(0, 0) ==
            Catch::Approx(0.0f));

    state.redo();
    REQUIRE(state.activeDocumentSession.document.getSample(0, 0) ==
            Catch::Approx(1.0f));
}

TEST_CASE("TriangleMarker drag updates cursor and selection through capture-aware moves",
          "[gui]")
{
    cupuacu::State state{};
    auto ui = cupuacu::test::createSessionUi(&state, 128, false, 2);
    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    viewState.samplesPerPixel = 2.0;

    state.activeDocumentSession.selection.setHighest(128);
    state.activeDocumentSession.selection.setValue1(10.0);
    state.activeDocumentSession.selection.setValue2(20.0);
    state.activeDocumentSession.cursor = 6;
    ui.mainView->updateTriangleMarkerBounds();

    auto *cursorTop = dynamic_cast<cupuacu::gui::TriangleMarker *>(
        findByNameRecursive(ui.mainView.get(), "TriangleMarker:CursorTop"));
    auto *selectionStart = dynamic_cast<cupuacu::gui::TriangleMarker *>(
        findByNameRecursive(ui.mainView.get(), "TriangleMarker:SelectionStartTop"));
    REQUIRE(cursorTop != nullptr);
    REQUIRE(selectionStart != nullptr);

    const auto downAtMarker =
        makeMouseEvent(cupuacu::gui::DOWN, 0.0f, 0.0f, 0.0f, true);
    REQUIRE(cursorTop->mouseDown(downAtMarker));
    REQUIRE_FALSE(cursorTop->mouseMove(
        makeMouseEvent(cupuacu::gui::MOVE, 4.0f, 0.0f, 0.0f, true)));

    auto *window = state.mainDocumentSessionWindow->getWindow();
    window->setCapturingComponent(cursorTop);
    REQUIRE(cursorTop->mouseMove(
        makeMouseEvent(cupuacu::gui::MOVE, 4.0f, 0.0f, 0.0f, true)));
    REQUIRE(state.activeDocumentSession.cursor == 14);

    REQUIRE(selectionStart->mouseDown(downAtMarker));
    window->setCapturingComponent(selectionStart);
    REQUIRE(selectionStart->mouseMove(
        makeMouseEvent(cupuacu::gui::MOVE, 5.0f, 0.0f, 0.0f, true)));
    REQUIRE(state.activeDocumentSession.selection.isActive());
    REQUIRE(state.activeDocumentSession.selection.getStartInt() == 20);
    REQUIRE(state.activeDocumentSession.selection.getEndExclusiveInt() == 21);
}
