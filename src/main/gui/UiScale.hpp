#pragma once

#include "../State.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace cupuacu::gui
{
#if defined(_WIN32)
    constexpr float kWindowsUiBaselineScale = 0.5f;
#endif

    inline float &getDisplayScaleFactor()
    {
        static float displayScaleFactor = 1.0f;
        return displayScaleFactor;
    }

    inline void setDisplayScaleFactor(const float scale)
    {
        getDisplayScaleFactor() = std::max(1.0f, scale);
    }

    inline float resolveAutomaticUiScale(const float displayScale)
    {
#if defined(__linux__)
        return std::max(0.25f, std::max(1.0f, displayScale) * 0.5f);
#else
        return std::max(1.0f, displayScale);
#endif
    }

    inline float getUiScale(const State *state)
    {
        return state ? std::max(0.25f, state->uiScale) : 1.0f;
    }

    inline float getCanvasSpaceScale(const State *state)
    {
        const int safePixelScale =
            std::max(1, static_cast<int>(state ? state->pixelScale : 1));
#if defined(_WIN32)
        const float displayScale = getDisplayScaleFactor();
        return getUiScale(state) * kWindowsUiBaselineScale * displayScale *
               displayScale /
               safePixelScale;
#else
        return getUiScale(state) / safePixelScale;
#endif
    }

    inline int scaleUi(const State *state, const float base,
                       const int minimum = 1)
    {
        return std::max(
            minimum,
            static_cast<int>(std::lround(base * getCanvasSpaceScale(state))));
    }

    inline float scaleUiF(const State *state, const float base,
                          const float minimum = 1.0f)
    {
        return std::max(minimum, base * getCanvasSpaceScale(state));
    }

    inline uint8_t scaleFontPointSize(const State *state, const int pointSize)
    {
        return static_cast<uint8_t>(
            std::clamp(scaleUi(state, static_cast<float>(pointSize)), 1, 255));
    }

} // namespace cupuacu::gui
