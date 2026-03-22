#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cupuacu::gui
{
    struct MainViewLayoutPlan
    {
        int borderWidth = 0;
        int scrollBarHeight = 0;
        int timelineHeight = 0;
        SDL_Rect topBorder{0, 0, 0, 0};
        SDL_Rect bottomBorder{0, 0, 0, 0};
        SDL_Rect leftBorder{0, 0, 0, 0};
        SDL_Rect rightBorder{0, 0, 0, 0};
        SDL_Rect horizontalScrollBar{0, 0, 0, 0};
        SDL_Rect waveforms{0, 0, 0, 0};
        SDL_Rect timeline{0, 0, 0, 0};
    };

    inline int scaleUiValue(const float uiScale, const uint8_t pixelScale,
                            const float base, const int minimum = 1)
    {
        const float safeUiScale = std::max(0.25f, uiScale);
        return std::max(minimum,
                        static_cast<int>(std::lround(base * safeUiScale)));
    }

    inline MainViewLayoutPlan planMainViewLayout(const int width, const int height,
                                                 const float uiScale,
                                                 const uint8_t pixelScale)
    {
        MainViewLayoutPlan plan{};
        constexpr float kTimelineLabelAreaHeight = 30.0f;
        constexpr float kTimelineLongTickHeight = 14.0f;
        plan.borderWidth = scaleUiValue(uiScale, pixelScale, 16.0f);
        plan.scrollBarHeight = scaleUiValue(uiScale, pixelScale, 14.0f);
        plan.timelineHeight = scaleUiValue(
            uiScale, pixelScale,
            kTimelineLabelAreaHeight + kTimelineLongTickHeight);

        plan.horizontalScrollBar = {
            plan.borderWidth,
            0,
            std::max(0, width - 2 * plan.borderWidth),
            plan.scrollBarHeight};
        plan.waveforms = {
            plan.borderWidth,
            plan.borderWidth + plan.scrollBarHeight,
            std::max(0, width - 2 * plan.borderWidth),
            std::max(0, height - 2 * plan.borderWidth - plan.timelineHeight -
                            plan.scrollBarHeight)};
        plan.topBorder = {0, 0, width, plan.borderWidth + plan.scrollBarHeight};
        plan.bottomBorder = {0, std::max(0, height - plan.borderWidth), width,
                             plan.borderWidth};
        plan.leftBorder = {0, plan.borderWidth + plan.scrollBarHeight,
                           plan.borderWidth,
                           std::max(0, height - 2 * plan.borderWidth -
                                           plan.scrollBarHeight)};
        plan.rightBorder = {std::max(0, width - plan.borderWidth),
                            plan.borderWidth + plan.scrollBarHeight,
                            plan.borderWidth,
                            std::max(0, height - 2 * plan.borderWidth -
                                            plan.scrollBarHeight)};
        plan.timeline = {plan.borderWidth,
                         std::max(0, height - plan.borderWidth -
                                         plan.timelineHeight),
                         std::max(0, width - 2 * plan.borderWidth),
                         plan.timelineHeight};
        return plan;
    }

    inline std::vector<SDL_Rect> planWaveformChannelTiles(const int width,
                                                          const int totalHeight,
                                                          const int channelCount)
    {
        std::vector<SDL_Rect> tiles;
        if (channelCount <= 0)
        {
            return tiles;
        }

        tiles.reserve(static_cast<size_t>(channelCount));
        const int baseHeight = totalHeight / channelCount;
        const int remainder = totalHeight % channelCount;

        int yPos = 0;
        for (int channel = 0; channel < channelCount; ++channel)
        {
            const int channelHeight = baseHeight + (channel < remainder ? 1 : 0);
            tiles.push_back(SDL_Rect{0, yPos, width, channelHeight});
            yPos += channelHeight;
        }
        return tiles;
    }
} // namespace cupuacu::gui
