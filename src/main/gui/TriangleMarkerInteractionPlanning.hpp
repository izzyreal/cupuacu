#pragma once

#include "TriangleMarker.hpp"

#include <cstdint>

namespace cupuacu::gui
{
    struct TriangleMarkerMouseDownPlan
    {
        double dragStartSample = 0.0;
        float dragMouseOffsetParentX = 0.0f;
        bool shouldFixSelectionOrder = false;
    };

    inline double planTriangleMarkerDragStartSample(
        const TriangleMarkerType type, const double selectionStart,
        const int64_t selectionEndExclusive, const int64_t cursor)
    {
        switch (type)
        {
            case TriangleMarkerType::SelectionStartTop:
            case TriangleMarkerType::SelectionStartBottom:
                return selectionStart;
            case TriangleMarkerType::SelectionEndTop:
            case TriangleMarkerType::SelectionEndBottom:
                return static_cast<double>(selectionEndExclusive);
            case TriangleMarkerType::CursorTop:
            case TriangleMarkerType::CursorBottom:
                return static_cast<double>(cursor);
        }
        return static_cast<double>(cursor);
    }

    inline TriangleMarkerMouseDownPlan planTriangleMarkerMouseDown(
        const TriangleMarkerType type, const double selectionStart,
        const int64_t selectionEndExclusive, const int64_t cursor,
        const float mouseParentX, const double samplesPerPixel,
        const bool selectionActive)
    {
        const double dragStartSample = planTriangleMarkerDragStartSample(
            type, selectionStart, selectionEndExclusive, cursor);
        const double mouseSample = mouseParentX * samplesPerPixel;
        return {dragStartSample,
                static_cast<float>(mouseSample - dragStartSample),
                selectionActive};
    }

    inline double planTriangleMarkerDraggedSamplePosition(
        const float mouseParentX, const double samplesPerPixel,
        const float dragMouseOffsetParentX)
    {
        const double mouseSample = mouseParentX * samplesPerPixel;
        return mouseSample - dragMouseOffsetParentX;
    }

    inline int64_t planTriangleMarkerSelectionValue(
        const double newSamplePos, const int64_t fixedEdge,
        const bool selectionWasActive)
    {
        const int64_t rounded =
            static_cast<int64_t>(std::llround(newSamplePos));
        return selectionWasActive && rounded == fixedEdge ? rounded + 1
                                                          : rounded;
    }
} // namespace cupuacu::gui
