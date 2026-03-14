#pragma once

#include "../State.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace cupuacu::gui
{
    inline float getUiScale(const State *state)
    {
        return state ? std::max(0.25f, state->uiScale) : 1.0f;
    }

    inline float getCanvasSpaceScale(const State *state)
    {
        const int safePixelScale =
            std::max(1, static_cast<int>(state ? state->pixelScale : 1));
        return getUiScale(state) / safePixelScale;
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

    inline float resolveInitialUiScale()
    {
        const char *value = SDL_getenv("CUPUACU_UI_SCALE");
        if (!value || value[0] == '\0')
        {
            return 1.0f;
        }

        char *end = nullptr;
        const float parsed = std::strtof(value, &end);
        if (end == value || !std::isfinite(parsed))
        {
            return 1.0f;
        }

        return std::max(0.25f, parsed);
    }
} // namespace cupuacu::gui
