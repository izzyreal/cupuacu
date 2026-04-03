#pragma once

#include "audio/AudioCallbackCore.hpp"
#include "audio/MeterFrame.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cupuacu::audio
{
    class ChannelMeterAccumulator
    {
    public:
        void addSample(const float sample)
        {
            peak = std::max(peak, std::abs(sample));
            sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
            ++sampleCount;
        }

        [[nodiscard]] MeterFrame finish() const
        {
            if (sampleCount == 0)
            {
                return {};
            }

            return {.peak = peak,
                    .rms = static_cast<float>(
                        std::sqrt(sumSquares / static_cast<double>(sampleCount)))};
        }

    private:
        float peak = 0.0f;
        double sumSquares = 0.0;
        uint64_t sampleCount = 0;
    };

    class StereoMeterAccumulator
    {
    public:
        void addFrame(const float left, const float right)
        {
            leftChannel.addSample(left);
            rightChannel.addSample(right);
        }

        void mergeInto(callback_core::StereoMeterLevels &meterLevels) const
        {
            const MeterFrame leftFrame = leftChannel.finish();
            const MeterFrame rightFrame = rightChannel.finish();

            meterLevels.peakLeft = std::max(meterLevels.peakLeft, leftFrame.peak);
            meterLevels.peakRight =
                std::max(meterLevels.peakRight, rightFrame.peak);
            meterLevels.rmsLeft = std::max(meterLevels.rmsLeft, leftFrame.rms);
            meterLevels.rmsRight = std::max(meterLevels.rmsRight, rightFrame.rms);
        }

    private:
        ChannelMeterAccumulator leftChannel;
        ChannelMeterAccumulator rightChannel;
    };
} // namespace cupuacu::audio
