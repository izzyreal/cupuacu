#pragma once

#include "Waveform.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace cupuacu::gui
{
    struct WaveformSamplePointLayout
    {
        int64_t sampleIndex = 0;
        int x = 0;
        int y = 0;
        uint16_t size = 0;
    };

    inline uint16_t getWaveformSamplePointSize(const uint8_t pixelScale)
    {
        return 32 / pixelScale;
    }

    inline float getWaveformCenterYForSampleValue(const float sampleValue,
                                                  const uint16_t waveformHeight,
                                                  const double verticalZoom,
                                                  const uint16_t samplePointSize)
    {
        const float drawableHeight = waveformHeight - samplePointSize;
        return waveformHeight * 0.5f -
               sampleValue * verticalZoom * drawableHeight * 0.5f;
    }

    inline std::vector<WaveformSamplePointLayout> planWaveformSamplePoints(
        const int width, const int height, const double samplesPerPixel,
        const int64_t sampleOffset, const uint8_t pixelScale,
        const double verticalZoom, const int64_t frameCount,
        const std::function<float(int64_t)> &getSampleValue)
    {
        if (width <= 0 || height <= 0 || samplesPerPixel <= 0.0 ||
            sampleOffset >= frameCount)
        {
            return {};
        }

        const float halfSampleWidth = 0.5f / samplesPerPixel;
        const int64_t neededInputSamples =
            static_cast<int64_t>(std::round(width * samplesPerPixel));
        const int64_t availableSamples = frameCount - sampleOffset;
        const int64_t actualInputSamples =
            std::min(neededInputSamples, availableSamples);

        if (actualInputSamples < 1)
        {
            return {};
        }

        std::vector<WaveformSamplePointLayout> result;
        const uint16_t samplePointSize = getWaveformSamplePointSize(pixelScale);
        result.reserve(static_cast<std::size_t>(actualInputSamples));

        for (int64_t i = 0; i < actualInputSamples; ++i)
        {
            const int sampleX = static_cast<int>(
                static_cast<double>(i) / samplesPerPixel + halfSampleWidth);
            const float sampleCenterY = getWaveformCenterYForSampleValue(
                getSampleValue(sampleOffset + i), static_cast<uint16_t>(height),
                verticalZoom, samplePointSize);

            result.push_back(WaveformSamplePointLayout{
                sampleOffset + i,
                std::max(0, sampleX - samplePointSize / 2),
                static_cast<int>(std::lround(
                    sampleCenterY - static_cast<float>(samplePointSize) * 0.5f)),
                samplePointSize});
        }

        return result;
    }
} // namespace cupuacu::gui
