#pragma once

#include <algorithm>

namespace cupuacu::gui
{
    struct SamplePointDragPlan
    {
        float clampedY = 0.0f;
        float sampleValue = 0.0f;
    };

    inline float getSamplePointSampleValueForCenterY(
        const float centerY, const uint16_t parentHeight,
        const double verticalZoom, const uint16_t samplePointSize)
    {
        const float drawableHeight = parentHeight - samplePointSize;
        return (parentHeight / 2.f - centerY) /
               (verticalZoom * (drawableHeight / 2.f));
    }

    inline SamplePointDragPlan planSamplePointDrag(
        const float currentDragY, const float mouseRelY,
        const uint16_t samplePointSize, const uint16_t parentHeight,
        const double verticalZoom)
    {
        const float maxY = parentHeight - samplePointSize;
        const float clampedY =
            std::clamp(currentDragY + mouseRelY, 0.0f, maxY);
        const float centerY = clampedY + samplePointSize * 0.5f;
        return {clampedY,
                std::clamp(getSamplePointSampleValueForCenterY(
                               centerY, parentHeight, verticalZoom,
                               samplePointSize),
                           -1.0f, 1.0f)};
    }
} // namespace cupuacu::gui
