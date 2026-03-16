#pragma once

#include "Selection.hpp"
#include "../SelectedChannels.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cupuacu::gui
{
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
} // namespace cupuacu::gui
