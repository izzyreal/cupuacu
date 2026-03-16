#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "gui/SamplePointInteractionPlanning.hpp"
#include "gui/TriangleMarkerInteractionPlanning.hpp"

using Catch::Approx;

TEST_CASE("TriangleMarker planning derives drag anchors and collapsed selection behavior",
          "[gui]")
{
    REQUIRE(cupuacu::gui::planTriangleMarkerDragStartSample(
                cupuacu::gui::TriangleMarkerType::SelectionStartTop, 10.0, 20,
                5) == Approx(10.0));
    REQUIRE(cupuacu::gui::planTriangleMarkerDragStartSample(
                cupuacu::gui::TriangleMarkerType::SelectionEndBottom, 10.0, 20,
                5) == Approx(20.0));
    REQUIRE(cupuacu::gui::planTriangleMarkerDragStartSample(
                cupuacu::gui::TriangleMarkerType::CursorTop, 10.0, 20, 5) ==
            Approx(5.0));

    const auto downPlan = cupuacu::gui::planTriangleMarkerMouseDown(
        cupuacu::gui::TriangleMarkerType::SelectionStartTop, 10.0, 20, 5,
        4.0f, 2.0, true);
    REQUIRE(downPlan.dragStartSample == Approx(10.0));
    REQUIRE(downPlan.dragMouseOffsetParentX == Approx(-2.0f));
    REQUIRE(downPlan.shouldFixSelectionOrder);

    REQUIRE(cupuacu::gui::planTriangleMarkerDraggedSamplePosition(9.0f, 2.0,
                                                                  -2.0f) ==
            Approx(20.0));
    REQUIRE(cupuacu::gui::planTriangleMarkerSelectionValue(20.0, 20, true) ==
            21);
    REQUIRE(cupuacu::gui::planTriangleMarkerSelectionValue(19.0, 20, true) ==
            19);
}

TEST_CASE("SamplePoint planning clamps drag and converts to sample values",
          "[gui]")
{
    const auto dragPlan = cupuacu::gui::planSamplePointDrag(
        10.0f, -500.0f, 8, 100, 1.0);
    REQUIRE(dragPlan.clampedY == Approx(0.0f));
    REQUIRE(dragPlan.sampleValue == Approx(1.0f));

    const auto lowerPlan = cupuacu::gui::planSamplePointDrag(
        10.0f, 500.0f, 8, 100, 1.0);
    REQUIRE(lowerPlan.clampedY == Approx(92.0f));
    REQUIRE(lowerPlan.sampleValue == Approx(-1.0f));

    REQUIRE(cupuacu::gui::getSamplePointSampleValueForCenterY(50.0f, 100, 1.0,
                                                              8) ==
            Approx(0.0f));
}
