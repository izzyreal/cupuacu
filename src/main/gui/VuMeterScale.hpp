#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace cupuacu::gui
{
    enum class VuMeterScale
    {
        PeakDbfs = 0,
        K20,
        K14,
        K12
    };

    struct VuMeterScaleConfig
    {
        float minDbfs = -72.0f;
        float maxDbfs = 0.0f;
        float warningDbfs = -12.0f;
        float redlineDbfs = -3.0f;
        float longTickSubdivisions = 3.0f;
        int intervalCount = 24;
        std::vector<std::string> labels;
        std::string endLabel = "0";
    };

    inline std::vector<std::string> buildKScaleLabels(const int maxPositiveK)
    {
        std::vector<std::string> labels;
        for (int value = -20; value < maxPositiveK; value += 2)
        {
            if (value > 0)
            {
                labels.push_back("+" + std::to_string(value));
            }
            else
            {
                labels.push_back(std::to_string(value));
            }
        }
        return labels;
    }

    inline VuMeterScaleConfig getVuMeterScaleConfig(const VuMeterScale scale)
    {
        switch (scale)
        {
        case VuMeterScale::K20:
            return VuMeterScaleConfig{
                .minDbfs = -40.0f,
                .maxDbfs = 0.0f,
                .warningDbfs = -20.0f,
                .redlineDbfs = -16.0f,
                .longTickSubdivisions = 2.0f,
                .intervalCount = 20,
                .labels = buildKScaleLabels(20),
                .endLabel = "+20"};
        case VuMeterScale::K14:
            return VuMeterScaleConfig{
                .minDbfs = -34.0f,
                .maxDbfs = 0.0f,
                .warningDbfs = -14.0f,
                .redlineDbfs = -10.0f,
                .longTickSubdivisions = 2.0f,
                .intervalCount = 17,
                .labels = buildKScaleLabels(14),
                .endLabel = "+14"};
        case VuMeterScale::K12:
            return VuMeterScaleConfig{
                .minDbfs = -32.0f,
                .maxDbfs = 0.0f,
                .warningDbfs = -12.0f,
                .redlineDbfs = -8.0f,
                .longTickSubdivisions = 2.0f,
                .intervalCount = 16,
                .labels = buildKScaleLabels(12),
                .endLabel = "+12"};
        case VuMeterScale::PeakDbfs:
        default:
            std::vector<std::string> labels;
            for (int db = -72; db < 0; db += 3)
            {
                labels.push_back(std::to_string(db));
            }
            return VuMeterScaleConfig{
                .minDbfs = -72.0f,
                .maxDbfs = 0.0f,
                .warningDbfs = -12.0f,
                .redlineDbfs = -3.0f,
                .longTickSubdivisions = 3.0f,
                .intervalCount = 24,
                .labels = std::move(labels),
                .endLabel = "0"};
        }
    }

    inline const std::vector<std::string> &vuMeterScaleOptionLabels()
    {
        static const std::vector<std::string> labels{
            "Peak dBFS", "K-20", "K-14", "K-12"};
        return labels;
    }

    inline int vuMeterScaleToIndex(const VuMeterScale scale)
    {
        switch (scale)
        {
        case VuMeterScale::K20:
            return 1;
        case VuMeterScale::K14:
            return 2;
        case VuMeterScale::K12:
            return 3;
        case VuMeterScale::PeakDbfs:
        default:
            return 0;
        }
    }

    inline VuMeterScale vuMeterScaleFromIndex(const int index)
    {
        switch (index)
        {
        case 1:
            return VuMeterScale::K20;
        case 2:
            return VuMeterScale::K14;
        case 3:
            return VuMeterScale::K12;
        case 0:
        default:
            return VuMeterScale::PeakDbfs;
        }
    }

    inline float normalizePeakForVuMeter(const float peak,
                                         const VuMeterScale scale)
    {
        if (!(peak > 0.0f))
        {
            return 0.0f;
        }

        const auto config = getVuMeterScaleConfig(scale);
        const float db = 20.0f * std::log10(std::max(peak, 1e-5f));
        float normalized =
            (db - config.minDbfs) / (config.maxDbfs - config.minDbfs);
        normalized = std::clamp(normalized, 0.0f, 1.0f);

        if (scale == VuMeterScale::PeakDbfs)
        {
            normalized = std::pow(normalized, 0.92f);
        }

        return normalized;
    }
} // namespace cupuacu::gui
