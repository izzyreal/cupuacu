#pragma once

#include "../SampleFormat.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace cupuacu::file
{
    inline bool isIntegerPcmSampleFormat(const cupuacu::SampleFormat format)
    {
        switch (format)
        {
            case cupuacu::SampleFormat::PCM_S8:
            case cupuacu::SampleFormat::PCM_S16:
            case cupuacu::SampleFormat::PCM_S24:
            case cupuacu::SampleFormat::PCM_S32:
                return true;
            case cupuacu::SampleFormat::FLOAT32:
            case cupuacu::SampleFormat::FLOAT64:
            case cupuacu::SampleFormat::Unknown:
            default:
                return false;
        }
    }

    inline int pcmBitDepth(const cupuacu::SampleFormat format)
    {
        switch (format)
        {
            case cupuacu::SampleFormat::PCM_S8:
                return 8;
            case cupuacu::SampleFormat::PCM_S16:
                return 16;
            case cupuacu::SampleFormat::PCM_S24:
                return 24;
            case cupuacu::SampleFormat::PCM_S32:
                return 32;
            case cupuacu::SampleFormat::FLOAT32:
                return 32;
            case cupuacu::SampleFormat::FLOAT64:
                return 64;
            case cupuacu::SampleFormat::Unknown:
            default:
                return 0;
        }
    }

    inline float clampNormalizedSample(const float sample)
    {
        return std::clamp(sample, -1.0f, 1.0f);
    }

    inline int64_t quantizeIntegerPcmSample(const cupuacu::SampleFormat format,
                                            const float sample,
                                            const bool preserveLoadedCode)
    {
        const int bitDepth = pcmBitDepth(format);
        if (!isIntegerPcmSampleFormat(format) || bitDepth <= 0)
        {
            return 0;
        }

        const int64_t minValue = -(int64_t{1} << (bitDepth - 1));
        const int64_t maxValue = (int64_t{1} << (bitDepth - 1)) - 1;
        const double scale = preserveLoadedCode
                                 ? static_cast<double>(int64_t{1}
                                                       << (bitDepth - 1))
                                 : static_cast<double>(maxValue);
        const double clamped = static_cast<double>(clampNormalizedSample(sample));
        const int64_t quantized = static_cast<int64_t>(std::llround(clamped * scale));
        return std::clamp(quantized, minValue, maxValue);
    }

    inline std::optional<int64_t>
    quantizedStatusSampleValue(const cupuacu::SampleFormat format,
                               const float sample,
                               const bool preserveLoadedCode)
    {
        if (!isIntegerPcmSampleFormat(format))
        {
            return std::nullopt;
        }

        return quantizeIntegerPcmSample(format, sample, preserveLoadedCode);
    }
} // namespace cupuacu::file
