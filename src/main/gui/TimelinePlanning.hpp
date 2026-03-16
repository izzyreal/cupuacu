#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace cupuacu::gui
{
    enum class TimelinePlanningMode
    {
        Decimal,
        Samples
    };

    struct TimelineRulerPlan
    {
        bool valid = false;
        float tickSpacingPx = 0.0f;
        int subdivisions = 1;
        float scrollOffsetPx = 0.0f;
        std::vector<std::string> labels;
    };

    inline bool planTimelineNeedsRefresh(const double lastSamplesPerPixel,
                                         const int64_t lastSampleOffset,
                                         const double currentSamplesPerPixel,
                                         const int64_t currentSampleOffset)
    {
        return lastSamplesPerPixel != currentSamplesPerPixel ||
               lastSampleOffset != currentSampleOffset;
    }

    inline int64_t planTimelineSampleIndexForXPos(const float xPos,
                                                  const int64_t sampleOffset,
                                                  const double samplesPerPixel)
    {
        return static_cast<int64_t>(
            std::llround(static_cast<double>(xPos) * samplesPerPixel +
                         static_cast<double>(sampleOffset)));
    }

    inline double planTimelineXPosForSampleIndex(const int64_t sampleIndex,
                                                 const int64_t sampleOffset,
                                                 const double samplesPerPixel)
    {
        return (static_cast<double>(sampleIndex) -
                static_cast<double>(sampleOffset)) /
               samplesPerPixel;
    }

    inline TimelineRulerPlan planTimelineRuler(
        const int waveformWidth, const uint8_t pixelScale,
        const int64_t sampleOffset, const double samplesPerPixel,
        const int64_t sampleRate, const TimelinePlanningMode mode,
        const bool showSamplePoints)
    {
        TimelineRulerPlan plan{};
        if (waveformWidth <= 0 || samplesPerPixel <= 0.0 || sampleRate <= 0)
        {
            return plan;
        }

        double maxTicks = waveformWidth * pixelScale / 85.0;
        maxTicks = std::max(1.0, maxTicks);

        const int totalVisibleSamples =
            static_cast<int>(planTimelineSampleIndexForXPos(
                                 waveformWidth - 1, sampleOffset,
                                 samplesPerPixel) -
                             sampleOffset);
        const int rawSamplesPerTick = std::max(
            1, static_cast<int>(std::ceil(static_cast<double>(totalVisibleSamples) /
                                          maxTicks)));

        static const std::vector<int> niceSteps = {
            1,       2,         5,         10,        20,        50,
            100,     200,       500,       1000,      2000,      5000,
            10'000,  20'000,    50'000,    100'000,   200'000,   400'000,
            800'000, 1'600'000, 3'200'000, 6'400'000, 12'800'000};

        int samplesPerTick = niceSteps.back();
        for (const int step : niceSteps)
        {
            if (step >= rawSamplesPerTick)
            {
                samplesPerTick = step;
                break;
            }
        }

        int firstSampleWithTick =
            (sampleOffset + samplesPerTick - 1) / samplesPerTick *
            samplesPerTick;
        firstSampleWithTick =
            std::max(0, firstSampleWithTick - samplesPerTick);

        const int lastVisibleSample = static_cast<int>(
            planTimelineSampleIndexForXPos(waveformWidth - 1, sampleOffset,
                                           samplesPerPixel));
        const int lastSampleWithTick =
            (lastVisibleSample + samplesPerTick - 1) / samplesPerTick *
            samplesPerTick;

        const float firstTickX = showSamplePoints
                                     ? static_cast<float>(std::round(
                                           planTimelineXPosForSampleIndex(
                                               firstSampleWithTick,
                                               sampleOffset, samplesPerPixel)))
                                     : static_cast<float>(
                                           planTimelineXPosForSampleIndex(
                                               firstSampleWithTick,
                                               sampleOffset, samplesPerPixel));

        const float lastTickX = showSamplePoints
                                    ? static_cast<float>(std::round(
                                          planTimelineXPosForSampleIndex(
                                              lastSampleWithTick, sampleOffset,
                                              samplesPerPixel)))
                                    : static_cast<float>(
                                          planTimelineXPosForSampleIndex(
                                              lastSampleWithTick, sampleOffset,
                                              samplesPerPixel));

        const int visibleTickCount =
            (lastSampleWithTick - firstSampleWithTick) / samplesPerTick;
        plan.tickSpacingPx = visibleTickCount > 0
                                 ? (lastTickX - firstTickX) / visibleTickCount
                                 : static_cast<float>(samplesPerTick);
        plan.scrollOffsetPx = firstTickX;
        plan.labels.push_back("smpl");

        for (int samplePos = firstSampleWithTick + samplesPerTick;
             samplePos <= lastSampleWithTick; samplePos += samplesPerTick)
        {
            if (mode == TimelinePlanningMode::Samples)
            {
                plan.labels.push_back(std::to_string(samplePos));
                continue;
            }

            const double seconds =
                static_cast<double>(samplePos) / sampleRate;
            const int mm = static_cast<int>(seconds / 60.0);
            const double ss = seconds - mm * 60.0;
            std::ostringstream oss;
            oss << mm << ":" << std::fixed << std::setprecision(3) << ss;
            plan.labels.push_back(oss.str());
        }

        if (samplesPerPixel < 1 / 200.0f)
        {
            plan.subdivisions = 1;
        }
        else
        {
            int temp = samplesPerTick;
            while (temp >= 10)
            {
                temp /= 10;
            }
            plan.subdivisions = temp == 2 ? 2 : 5;
        }

        plan.valid = true;
        return plan;
    }
} // namespace cupuacu::gui
