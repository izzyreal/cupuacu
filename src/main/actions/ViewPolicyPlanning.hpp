#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cupuacu::actions
{
    struct DurationChangeViewPolicyPlan
    {
        bool shouldResetZoomToFillWidth = false;
    };

    inline DurationChangeViewPolicyPlan planDurationChangeViewPolicy(
        const int64_t frameCount, const double waveformWidth,
        const double samplesPerPixel)
    {
        DurationChangeViewPolicyPlan plan{};
        const bool hasInvalidHorizontalZoom = samplesPerPixel <= 0.0;
        const bool hasViewWiderThanDocument =
            frameCount > 0 && waveformWidth > 0.0 &&
            std::ceil(waveformWidth * samplesPerPixel) >
                static_cast<double>(frameCount);

        plan.shouldResetZoomToFillWidth =
            hasInvalidHorizontalZoom || hasViewWiderThanDocument;
        return plan;
    }
} // namespace cupuacu::actions
