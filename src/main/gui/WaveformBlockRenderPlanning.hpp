#pragma once

#include "WaveformCache.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace cupuacu::gui
{
    struct BlockWaveformColumnPlan
    {
        int drawXi = 0;
        int y1 = 0;
        int y2 = 0;
        int midY = 0;
        bool connectFromPrevious = false;
        int previousX = 0;
        int previousY = 0;
    };

    struct BlockWaveformPeakColumnPlan
    {
        int drawXi = 0;
        Peak peak{};
    };

    template <typename PeakLookup>
    std::vector<BlockWaveformPeakColumnPlan> planBlockWaveformPeakColumns(
        const int widthToUse, const double blockRenderPhasePx,
        PeakLookup &&lookupPeak)
    {
        std::vector<BlockWaveformPeakColumnPlan> result;
        int lastDrawXi = std::numeric_limits<int>::min();

        for (int x = 0; x < widthToUse + 1; ++x)
        {
            Peak p;
            if (!lookupPeak(x, p))
            {
                continue;
            }

            const float drawX = static_cast<float>(x - blockRenderPhasePx);
            const int drawXi = static_cast<int>(std::lround(drawX));
            if (drawXi < 0 || drawXi > widthToUse)
            {
                continue;
            }

            if (drawXi == lastDrawXi)
            {
                continue;
            }

            result.push_back(BlockWaveformPeakColumnPlan{
                .drawXi = drawXi,
                .peak = p,
            });
            lastDrawXi = drawXi;
        }

        return result;
    }

    template <typename PeakLookup>
    std::vector<BlockWaveformColumnPlan> planBlockWaveformColumns(
        const int widthToUse, const double blockRenderPhasePx,
        const int centerY, const float scale, PeakLookup &&lookupPeak)
    {
        std::vector<BlockWaveformColumnPlan> result;

        int prevX = 0;
        int prevY = 0;
        bool hasPrev = false;
        int lastDrawXi = std::numeric_limits<int>::min();

        for (int x = 0; x < widthToUse + 1; ++x)
        {
            Peak p;
            if (!lookupPeak(x, p))
            {
                continue;
            }

            const float drawX = static_cast<float>(x - blockRenderPhasePx);
            const int drawXi = static_cast<int>(std::lround(drawX));
            if (drawXi < 0 || drawXi > widthToUse)
            {
                continue;
            }

            if (drawXi == lastDrawXi)
            {
                continue;
            }

            BlockWaveformColumnPlan column{};
            column.drawXi = drawXi;
            column.y1 = static_cast<int>(centerY - p.max * scale);
            column.y2 = static_cast<int>(centerY - p.min * scale);
            column.midY = (column.y1 + column.y2) / 2;
            column.connectFromPrevious = hasPrev && prevX != drawXi;
            column.previousX = prevX;
            column.previousY = prevY;
            result.push_back(column);

            prevX = drawXi;
            prevY = column.midY;
            hasPrev = true;
            lastDrawXi = drawXi;
        }

        return result;
    }
} // namespace cupuacu::gui
