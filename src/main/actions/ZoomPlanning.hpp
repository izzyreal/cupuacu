#pragma once

#include "../Constants.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cupuacu::actions
{
    struct ZoomResetPlan
    {
        double samplesPerPixel = INITIAL_SAMPLES_PER_PIXEL;
        double verticalZoom = INITIAL_VERTICAL_ZOOM;
        int64_t sampleOffset = INITIAL_SAMPLE_OFFSET;
    };

    struct HorizontalZoomPlan
    {
        bool changed = false;
        double samplesPerPixel = INITIAL_SAMPLES_PER_PIXEL;
        int64_t sampleOffset = INITIAL_SAMPLE_OFFSET;
    };

    struct ZoomSelectionPlan
    {
        bool changed = false;
        double samplesPerPixel = INITIAL_SAMPLES_PER_PIXEL;
        double verticalZoom = INITIAL_VERTICAL_ZOOM;
        int64_t sampleOffset = INITIAL_SAMPLE_OFFSET;
    };

    inline double planMinSamplesPerPixel(const int waveformWidth)
    {
        if (waveformWidth <= 0)
        {
            return 0.0;
        }
        return 1.0 / static_cast<double>(waveformWidth);
    }

    inline int64_t planMaxSampleOffset(const int64_t frameCount,
                                       const int waveformWidth,
                                       const double samplesPerPixel)
    {
        if (waveformWidth <= 0 || frameCount <= 0 || samplesPerPixel <= 0.0)
        {
            return 0;
        }

        const int64_t visibleSampleCount = static_cast<int64_t>(
            std::ceil(static_cast<double>(waveformWidth) * samplesPerPixel));
        return std::max<int64_t>(0, frameCount - visibleSampleCount);
    }

    inline ZoomResetPlan planResetZoom(const int64_t frameCount,
                                       const int waveformWidth)
    {
        ZoomResetPlan plan{};
        if (waveformWidth <= 0)
        {
            plan.samplesPerPixel = 0.0;
            return plan;
        }

        plan.samplesPerPixel =
            static_cast<double>(frameCount) / static_cast<double>(waveformWidth);
        return plan;
    }

    inline HorizontalZoomPlan
    planZoomInHorizontally(const double currentSamplesPerPixel,
                           const int64_t currentSampleOffset,
                           const int waveformWidth, const int64_t frameCount)
    {
        HorizontalZoomPlan plan{
            .changed = false,
            .samplesPerPixel = currentSamplesPerPixel,
            .sampleOffset = currentSampleOffset,
        };
        if (waveformWidth <= 0 || currentSamplesPerPixel <= 0.0)
        {
            return plan;
        }

        const double minSamplesPerPixel =
            planMinSamplesPerPixel(waveformWidth);
        if (currentSamplesPerPixel <= minSamplesPerPixel)
        {
            return plan;
        }

        const double centerSampleIndex =
            ((waveformWidth / 2.0 + 0.5) * currentSamplesPerPixel) +
            static_cast<double>(currentSampleOffset);
        plan.samplesPerPixel =
            std::max(currentSamplesPerPixel / 2.0, minSamplesPerPixel);
        const double requestedOffset =
            centerSampleIndex -
            ((waveformWidth / 2.0 + 0.5) * plan.samplesPerPixel);
        plan.sampleOffset = std::clamp<int64_t>(
            static_cast<int64_t>(requestedOffset), 0,
            planMaxSampleOffset(frameCount, waveformWidth,
                                plan.samplesPerPixel));
        plan.changed = true;
        return plan;
    }

    inline HorizontalZoomPlan
    planZoomOutHorizontally(const double currentSamplesPerPixel,
                            const int64_t currentSampleOffset,
                            const int waveformWidth, const int64_t frameCount)
    {
        HorizontalZoomPlan plan{
            .changed = false,
            .samplesPerPixel = currentSamplesPerPixel,
            .sampleOffset = currentSampleOffset,
        };
        if (waveformWidth <= 0 || currentSamplesPerPixel <= 0.0)
        {
            return plan;
        }

        const double maxSamplesPerPixel =
            static_cast<double>(frameCount) / static_cast<double>(waveformWidth);
        if (currentSamplesPerPixel >= maxSamplesPerPixel)
        {
            return plan;
        }

        const double centerSampleIndex =
            ((waveformWidth / 2.0 + 0.5) * currentSamplesPerPixel) +
            static_cast<double>(currentSampleOffset);
        plan.samplesPerPixel =
            std::min(currentSamplesPerPixel * 2.0, maxSamplesPerPixel);
        const double requestedOffset =
            centerSampleIndex -
            ((waveformWidth / 2.0 + 0.5) * plan.samplesPerPixel);
        plan.sampleOffset = std::clamp<int64_t>(
            static_cast<int64_t>(requestedOffset), 0,
            planMaxSampleOffset(frameCount, waveformWidth,
                                plan.samplesPerPixel));
        plan.changed = true;
        return plan;
    }

    inline ZoomSelectionPlan planZoomSelection(const bool selectionIsActive,
                                               const int64_t selectionLength,
                                               const int64_t selectionStart,
                                               const int waveformWidth)
    {
        ZoomSelectionPlan plan{};
        if (!selectionIsActive || selectionLength < 1 || waveformWidth <= 0)
        {
            return plan;
        }

        plan.changed = true;
        plan.samplesPerPixel =
            static_cast<double>(selectionLength) / waveformWidth;
        plan.sampleOffset = selectionStart;
        return plan;
    }
} // namespace cupuacu::actions
