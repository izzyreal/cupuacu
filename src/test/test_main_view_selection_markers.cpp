#include <catch2/catch_test_macros.hpp>

#include "TestStateBuilders.hpp"
#include "gui/Component.hpp"
#include "gui/Waveform.hpp"
#include "gui/WaveformCache.hpp"

#include <string_view>

namespace
{
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
} // namespace

TEST_CASE("Selection markers align with block selection edges", "[gui]")
{
    cupuacu::State state{};
    [[maybe_unused]] auto ui = cupuacu::test::createSessionUi(&state, 800000);

    auto &session = state.activeDocumentSession;
    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    REQUIRE_FALSE(state.waveforms.empty());

    session.selection.setValue1(15321.0);
    session.selection.setValue2(22123.0);
    viewState.sampleOffset = 12000;
    viewState.samplesPerPixel = 700.0; // block mode with cache

    ui.mainView->updateTriangleMarkerBounds();

    const auto *selStartTop =
        findByNameRecursive(ui.mainView.get(), "TriangleMarker:SelectionStartTop");
    const auto *selStartBottom = findByNameRecursive(
        ui.mainView.get(), "TriangleMarker:SelectionStartBottom");
    const auto *selEndTop =
        findByNameRecursive(ui.mainView.get(), "TriangleMarker:SelectionEndTop");
    const auto *selEndBottom =
        findByNameRecursive(ui.mainView.get(), "TriangleMarker:SelectionEndBottom");
    const auto *cursorTop =
        findByNameRecursive(ui.mainView.get(), "TriangleMarker:CursorTop");
    const auto *cursorBottom =
        findByNameRecursive(ui.mainView.get(), "TriangleMarker:CursorBottom");

    REQUIRE(selStartTop != nullptr);
    REQUIRE(selStartBottom != nullptr);
    REQUIRE(selEndTop != nullptr);
    REQUIRE(selEndBottom != nullptr);
    REQUIRE(cursorTop != nullptr);
    REQUIRE(cursorBottom != nullptr);

    REQUIRE(selStartTop->isVisible());
    REQUIRE(selStartBottom->isVisible());
    REQUIRE(selEndTop->isVisible());
    REQUIRE(selEndBottom->isVisible());
    REQUIRE_FALSE(cursorTop->isVisible());
    REQUIRE_FALSE(cursorBottom->isVisible());

    int64_t samplesPerPeakForDisplay = 1;
    if (viewState.samplesPerPixel >= cupuacu::gui::WaveformCache::BASE_BLOCK_SIZE)
    {
        const auto &waveformCache = session.document.getWaveformCache(0);
        const int cacheLevel = waveformCache.getLevelIndex(viewState.samplesPerPixel);
        samplesPerPeakForDisplay =
            waveformCache.samplesPerPeakForLevel(cacheLevel);
    }

    int32_t expectedStartEdge = 0;
    int32_t expectedEndEdge = 0;
    const bool hasSelectionEdges =
        cupuacu::gui::Waveform::computeBlockModeSelectionEdgePixels(
            session.selection.getStartInt(), session.selection.getEndInt() + 1,
            viewState.sampleOffset, viewState.samplesPerPixel,
            state.waveforms[0]->getWidth(), expectedStartEdge, expectedEndEdge,
            samplesPerPeakForDisplay, true);
    REQUIRE(hasSelectionEdges);

    const int waveformsAbsoluteX = state.waveforms[0]->getAbsoluteBounds().x;
    const int expectedStartAbsX = waveformsAbsoluteX + expectedStartEdge;
    const int expectedEndAbsX = waveformsAbsoluteX + expectedEndEdge;

    const SDL_Rect startTopBounds = selStartTop->getAbsoluteBounds();
    const SDL_Rect startBottomBounds = selStartBottom->getAbsoluteBounds();
    const SDL_Rect endTopBounds = selEndTop->getAbsoluteBounds();
    const SDL_Rect endBottomBounds = selEndBottom->getAbsoluteBounds();

    // Start marker's left vertical edge must match selection start.
    REQUIRE(startTopBounds.x == expectedStartAbsX);
    REQUIRE(startBottomBounds.x == expectedStartAbsX);

    // End marker's right vertical edge must match selection end.
    REQUIRE(endTopBounds.x + endTopBounds.w == expectedEndAbsX);
    REQUIRE(endBottomBounds.x + endBottomBounds.w == expectedEndAbsX);
}

TEST_CASE("Cursor markers align with cursor when selection is inactive", "[gui]")
{
    cupuacu::State state{};
    [[maybe_unused]] auto ui = cupuacu::test::createSessionUi(&state, 800000);

    auto &session = state.activeDocumentSession;
    auto &viewState = state.mainDocumentSessionWindow->getViewState();
    REQUIRE_FALSE(state.waveforms.empty());

    session.selection.reset();
    session.cursor = 15321;
    viewState.sampleOffset = 12000;
    viewState.samplesPerPixel = 700.0;

    ui.mainView->updateTriangleMarkerBounds();

    const auto *selStartTop =
        findByNameRecursive(ui.mainView.get(), "TriangleMarker:SelectionStartTop");
    const auto *selStartBottom = findByNameRecursive(
        ui.mainView.get(), "TriangleMarker:SelectionStartBottom");
    const auto *selEndTop =
        findByNameRecursive(ui.mainView.get(), "TriangleMarker:SelectionEndTop");
    const auto *selEndBottom =
        findByNameRecursive(ui.mainView.get(), "TriangleMarker:SelectionEndBottom");
    const auto *cursorTop =
        findByNameRecursive(ui.mainView.get(), "TriangleMarker:CursorTop");
    const auto *cursorBottom =
        findByNameRecursive(ui.mainView.get(), "TriangleMarker:CursorBottom");

    REQUIRE(selStartTop != nullptr);
    REQUIRE(selStartBottom != nullptr);
    REQUIRE(selEndTop != nullptr);
    REQUIRE(selEndBottom != nullptr);
    REQUIRE(cursorTop != nullptr);
    REQUIRE(cursorBottom != nullptr);

    REQUIRE_FALSE(selStartTop->isVisible());
    REQUIRE_FALSE(selStartBottom->isVisible());
    REQUIRE_FALSE(selEndTop->isVisible());
    REQUIRE_FALSE(selEndBottom->isVisible());
    REQUIRE(cursorTop->isVisible());
    REQUIRE(cursorBottom->isVisible());

    const int cursorX = cupuacu::gui::Waveform::getXPosForSampleIndex(
        session.cursor, viewState.sampleOffset, viewState.samplesPerPixel);
    const int expectedCursorAbsX =
        state.waveforms[0]->getAbsoluteBounds().x + cursorX;

    const SDL_Rect topBounds = cursorTop->getAbsoluteBounds();
    const SDL_Rect bottomBounds = cursorBottom->getAbsoluteBounds();

    REQUIRE(topBounds.x + topBounds.w / 2 - 1 == expectedCursorAbsX);
    REQUIRE(bottomBounds.x + bottomBounds.w / 2 - 1 == expectedCursorAbsX);
}
