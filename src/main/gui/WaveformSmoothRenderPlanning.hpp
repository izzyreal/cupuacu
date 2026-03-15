#pragma once

#include "smooth_line.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace cupuacu::gui
{
    struct WaveformSmoothRenderInput
    {
        std::vector<double> sampleX;
        std::vector<double> sampleY;
        std::vector<double> queryX;
    };

    struct WaveformSmoothSegmentQuad
    {
        std::array<SDL_FPoint, 4> vertices{};
    };

    inline WaveformSmoothRenderInput planWaveformSmoothRenderInput(
        const int width, const double samplesPerPixel,
        const int64_t sampleOffset, const double halfSampleWidth,
        const int64_t frameCount,
        const std::function<float(int64_t)> &getSampleValue)
    {
        WaveformSmoothRenderInput input{};
        if (width <= 0 || samplesPerPixel <= 0.0 || sampleOffset >= frameCount)
        {
            return input;
        }

        const int64_t leadingSamples = sampleOffset > 0 ? 1 : 0;
        const int64_t trailingSamples =
            sampleOffset < frameCount ? 1 : 0;
        const int64_t neededInputSamples =
            static_cast<int64_t>(std::ceil((width + 1) * samplesPerPixel));
        const int64_t fetchStart = std::max<int64_t>(0, sampleOffset - leadingSamples);
        const int64_t availableSamples = frameCount - fetchStart;
        const int64_t actualInputSamples =
            std::min(neededInputSamples + leadingSamples + trailingSamples,
                     availableSamples);
        if (actualInputSamples < 1)
        {
            return input;
        }

        input.sampleX.resize(static_cast<std::size_t>(actualInputSamples));
        input.sampleY.resize(static_cast<std::size_t>(actualInputSamples));
        for (int64_t i = 0; i < actualInputSamples; ++i)
        {
            const int64_t sampleIndex = fetchStart + i;
            input.sampleX[static_cast<std::size_t>(i)] =
                (static_cast<double>(sampleIndex - sampleOffset) /
                 samplesPerPixel) +
                halfSampleWidth;
            input.sampleY[static_cast<std::size_t>(i)] =
                static_cast<double>(getSampleValue(sampleIndex));
        }

        input.queryX.resize(static_cast<std::size_t>(width + 1));
        for (int i = 0; i <= width; ++i)
        {
            input.queryX[static_cast<std::size_t>(i)] = static_cast<double>(i);
        }

        return input;
    }

    inline std::optional<WaveformSmoothSegmentQuad> planWaveformSmoothSegmentQuad(
        const float x1, const float x2, const float y1, const float y2,
        const float thickness)
    {
        float dx = x2 - x1;
        float dy = y2 - y1;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len == 0.0f)
        {
            return std::nullopt;
        }

        dx /= len;
        dy /= len;

        const float px = -dy * thickness * 0.5f;
        const float py = dx * thickness * 0.5f;

        WaveformSmoothSegmentQuad quad{};
        quad.vertices[0] = {x1 - px, y1 - py};
        quad.vertices[1] = {x1 + px, y1 + py};
        quad.vertices[2] = {x2 + px, y2 + py};
        quad.vertices[3] = {x2 - px, y2 - py};
        return quad;
    }
} // namespace cupuacu::gui
