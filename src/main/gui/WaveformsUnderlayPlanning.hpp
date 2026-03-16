#pragma once

#include "Selection.hpp"
#include "../SelectedChannels.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cupuacu::gui
{
    struct WaveformsUnderlayAutoScrollPlan
    {
        double samplesToScroll = 0.0;
    };

    struct WaveformsUnderlayWheelStepPlan
    {
        double stepPixels = 0.0;
        double remainingPendingPixels = 0.0;
    };

    struct WaveformsUnderlayWheelDeltaPlan
    {
        int64_t wholeSamples = 0;
        double remainingSamples = 0.0;
    };

    inline SelectedChannels planWaveformsUnderlayHoveredChannels(
        const int32_t mouseY, const int height, const int channelCount)
    {
        if (channelCount <= 0 || height <= 0)
        {
            return BOTH;
        }

        const int channelHeight = std::max(1, height / channelCount);
        if (mouseY < channelHeight / 4)
        {
            return LEFT;
        }
        if (mouseY >= height - channelHeight / 4)
        {
            return RIGHT;
        }
        return BOTH;
    }

    inline double planWaveformsUnderlaySamplePosition(
        const int64_t sampleOffset, const double samplesPerPixel,
        const float mouseX)
    {
        return static_cast<double>(sampleOffset) + mouseX * samplesPerPixel;
    }

    inline bool planWaveformsUnderlayVisibleRangeSelection(
        const int64_t frameCount, const int64_t sampleOffset,
        const double samplesPerPixel, const int width, double &startOut,
        double &endOut)
    {
        startOut = 0.0;
        endOut = 0.0;
        if (frameCount <= 0 || samplesPerPixel <= 0.0 || width <= 0)
        {
            return false;
        }

        startOut = static_cast<double>(sampleOffset);
        endOut = std::min(static_cast<double>(frameCount),
                          static_cast<double>(sampleOffset) +
                              width * samplesPerPixel);

        if (samplesPerPixel < 1.0)
        {
            const double endFloor = std::floor(endOut);
            const double coverage = endOut - endFloor;
            endOut = coverage < 1.0 ? endFloor : endFloor + 1.0;
        }

        return endOut > startOut;
    }

    inline int64_t planWaveformsUnderlayValidSampleIndex(
        const int32_t mouseX, const double samplesPerPixel,
        const int64_t sampleOffset, const int64_t frameCount)
    {
        if (frameCount <= 0)
        {
            return 0;
        }
        const int64_t sampleIndex =
            static_cast<int64_t>(mouseX * samplesPerPixel) + sampleOffset;
        return std::clamp(sampleIndex, int64_t{0}, frameCount - 1);
    }

    inline void applyWaveformsUnderlayDraggedSelection(
        Selection<double> &selection, const int64_t sampleOffset,
        const double samplesPerPixel, const int32_t mouseX)
    {
        const bool selectionWasActive = selection.isActive();
        const double samplePos =
            static_cast<double>(sampleOffset) +
            static_cast<double>(mouseX) * samplesPerPixel;
        selection.setValue2(samplePos);
        if (selectionWasActive && !selection.isActive())
        {
            selection.setValue2(samplePos + 1.0);
        }
    }

    inline WaveformsUnderlayAutoScrollPlan
    planWaveformsUnderlayAutoScroll(const int32_t mouseX, const int width,
                                    const double samplesPerPixel)
    {
        WaveformsUnderlayAutoScrollPlan plan{};
        if (width <= 0 || samplesPerPixel <= 0.0)
        {
            return plan;
        }

        if (mouseX > width || mouseX < 0)
        {
            const int32_t diff = mouseX < 0 ? mouseX : mouseX - width;
            plan.samplesToScroll =
                static_cast<double>(diff) * samplesPerPixel;
        }

        return plan;
    }

    inline WaveformsUnderlayWheelStepPlan
    planWaveformsUnderlayWheelStep(const double pendingPixels,
                                   const uint64_t lastWheelEventTicks,
                                   const uint64_t nowTicks,
                                   const double snapThresholdPixels = 0.5,
                                   const double smoothingFactor = 0.3,
                                   const uint64_t streamTimeoutMs = 20)
    {
        WaveformsUnderlayWheelStepPlan plan{};
        if (pendingPixels == 0.0)
        {
            return plan;
        }

        const bool wheelStreamTimedOut =
            lastWheelEventTicks > 0 &&
            nowTicks > lastWheelEventTicks + streamTimeoutMs;
        const double appliedSmoothing =
            wheelStreamTimedOut ? 1.0 : smoothingFactor;
        const double absPending = std::abs(pendingPixels);
        plan.stepPixels =
            absPending <= snapThresholdPixels
                ? pendingPixels
                : pendingPixels * appliedSmoothing;
        plan.remainingPendingPixels = pendingPixels - plan.stepPixels;
        if (std::abs(plan.remainingPendingPixels) < 1e-6)
        {
            plan.remainingPendingPixels = 0.0;
        }
        return plan;
    }

    inline WaveformsUnderlayWheelDeltaPlan
    planWaveformsUnderlayWheelDelta(const double sampleRemainder,
                                    const double stepPixels,
                                    const double samplesPerPixel)
    {
        WaveformsUnderlayWheelDeltaPlan plan{};
        if (samplesPerPixel <= 0.0 || stepPixels == 0.0)
        {
            plan.remainingSamples = sampleRemainder;
            return plan;
        }

        const double totalSamples =
            sampleRemainder + stepPixels * samplesPerPixel;
        plan.wholeSamples = static_cast<int64_t>(std::trunc(totalSamples));
        plan.remainingSamples =
            totalSamples - static_cast<double>(plan.wholeSamples);
        return plan;
    }
} // namespace cupuacu::gui
