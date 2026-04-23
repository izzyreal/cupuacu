#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestPaths.hpp"
#include "gui/SnapPlanning.hpp"
#include "gui/Waveform.hpp"
#include "gui/WaveformsUnderlayPlanning.hpp"

TEST_CASE("WaveformsUnderlay single click planning uses hovered sample position",
          "[gui]")
{
    const double samplePos =
        cupuacu::gui::planWaveformsUnderlaySamplePosition(10, 1.0, 7.0f);
    REQUIRE(samplePos == 17.0);
}

TEST_CASE("WaveformsUnderlay double click planning selects visible range",
          "[gui]")
{
    double start = 0.0;
    double end = 0.0;
    REQUIRE(cupuacu::gui::planWaveformsUnderlayVisibleRangeSelection(
        200, 25, 1.0, 50, start, end));
    REQUIRE(start == 25.0);
    REQUIRE(end == 75.0);
}

TEST_CASE("WaveformsUnderlay drag planning updates selection and right-channel hover",
          "[gui]")
{
    cupuacu::gui::Selection<double> selection(0.0);
    selection.setHighest(100.0);
    selection.setValue1(10.0);

    cupuacu::gui::applyWaveformsUnderlayDraggedSelection(selection, 0, 1.0, 20);

    REQUIRE(selection.isActive());
    REQUIRE(selection.getStartInt() == 10);
    REQUIRE(selection.getEndInt() == 19);
    REQUIRE(cupuacu::gui::planWaveformsUnderlayHoveredChannels(79, 80, 2) ==
            cupuacu::RIGHT);
}

TEST_CASE("WaveformsUnderlay mono hover planning keeps channel selection unified",
          "[gui]")
{
    REQUIRE(cupuacu::gui::planWaveformsUnderlayHoveredChannels(0, 80, 1) ==
            cupuacu::BOTH);
    REQUIRE(cupuacu::gui::planWaveformsUnderlayHoveredChannels(79, 80, 1) ==
            cupuacu::BOTH);
}

TEST_CASE("WaveformsUnderlay snap planning bounds visible selection by markers",
          "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.snapEnabled = true;
    state.getActiveDocumentSession().document.initialize(
        cupuacu::SampleFormat::FLOAT32, 44100, 1, 200);
    state.getActiveDocumentSession().document.addMarker(35, "Left");
    state.getActiveDocumentSession().document.addMarker(70, "Right");
    state.getActiveViewState().sampleOffset = 20;
    state.getActiveViewState().samplesPerPixel = 1.0;

    auto waveform = std::make_unique<cupuacu::gui::Waveform>(&state, 0);
    waveform->setSize(100, 40);
    state.waveforms.push_back(waveform.get());

    int64_t start = 0;
    int64_t end = 0;
    REQUIRE(cupuacu::gui::planSnappedVisibleRangeSelection(&state, 30.0f, start,
                                                           end));
    REQUIRE(start == 35);
    REQUIRE(end == 70);
}

TEST_CASE("Snap planning pulls cursor drags to nearby markers and edges", "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.snapEnabled = true;
    state.getActiveDocumentSession().document.initialize(
        cupuacu::SampleFormat::FLOAT32, 44100, 1, 200);
    state.getActiveDocumentSession().document.addMarker(80, "Hit");
    state.getActiveDocumentSession().cursor = 140;

    const int64_t snappedToMarker = cupuacu::gui::snapSamplePosition(
        &state, 82, std::nullopt, false, std::nullopt, 0, 1.0, 200);
    REQUIRE(snappedToMarker == 80);

    const int64_t snappedToCursor = cupuacu::gui::snapSamplePosition(
        &state, 143, 1, true, std::nullopt, 0, 1.0, 200);
    REQUIRE(snappedToCursor == 140);

    const int64_t snappedToStart = cupuacu::gui::snapSamplePosition(
        &state, 3, std::nullopt, false, std::nullopt, 0, 1.0, 200);
    REQUIRE(snappedToStart == 0);
}

TEST_CASE("Snap planning includes selection edges as targets and excludes self",
          "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.snapEnabled = true;
    state.getActiveDocumentSession().document.initialize(
        cupuacu::SampleFormat::FLOAT32, 44100, 1, 200);
    state.getActiveDocumentSession().selection.setHighest(200.0);
    state.getActiveDocumentSession().selection.setValue1(40.0);
    state.getActiveDocumentSession().selection.setValue2(90.0);

    const int64_t snappedToSelectionStart = cupuacu::gui::snapSamplePosition(
        &state, 43, std::nullopt, false, std::nullopt, 0, 1.0, 200);
    REQUIRE(snappedToSelectionStart == 40);

    const int64_t snappedToSelectionEnd = cupuacu::gui::snapSamplePosition(
        &state, 87, std::nullopt, false, std::nullopt, 0, 1.0, 200);
    REQUIRE(snappedToSelectionEnd == 90);

    const int64_t startDragDoesNotSnapToItself = cupuacu::gui::snapSamplePosition(
        &state, 42, std::nullopt, true, cupuacu::gui::SnapSelectionEdge::Start,
        0, 1.0, 200);
    REQUIRE(startDragDoesNotSnapToItself != 40);
    REQUIRE(startDragDoesNotSnapToItself == 42);
}

TEST_CASE("Snap planning maps mouse positions for waveform click and drag",
          "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.snapEnabled = true;
    state.getActiveDocumentSession().document.initialize(
        cupuacu::SampleFormat::FLOAT32, 44100, 1, 200);
    state.getActiveDocumentSession().document.addMarker(30, "A");
    state.getActiveDocumentSession().selection.setHighest(200.0);
    state.getActiveDocumentSession().selection.setValue1(60.0);
    state.getActiveDocumentSession().selection.setValue2(100.0);
    state.getActiveDocumentSession().cursor = 150;
    state.getActiveViewState().sampleOffset = 0;
    state.getActiveViewState().samplesPerPixel = 1.0;

    auto waveform = std::make_unique<cupuacu::gui::Waveform>(&state, 0);
    waveform->setSize(200, 40);
    state.waveforms.push_back(waveform.get());

    REQUIRE(cupuacu::gui::planSnappedMouseSamplePosition(&state, 28.0f, true) ==
            30);
    REQUIRE(cupuacu::gui::planSnappedMouseSamplePosition(&state, 58.0f, true) ==
            60);
    REQUIRE(cupuacu::gui::planSnappedMouseSamplePosition(&state, 147.0f, true) ==
            150);
}
