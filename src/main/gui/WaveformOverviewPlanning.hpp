#pragma once

#include "../Document.hpp"
#include "Waveform.hpp"
#include "WaveformBlockRenderPlanning.hpp"
#include "WaveformCache.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace cupuacu::gui
{
    inline bool computeWaveformPeakForSampleWindow(
        const cupuacu::Document &document, const int channelIndex,
        const int64_t sampleOffset, const double samplesPerPixel,
        const uint8_t pixelScale, const double startSampleInclusive,
        const double endSampleExclusive, Peak &outPeak)
    {
        const auto &sampleData =
            document.getAudioBuffer()->getImmutableChannelData(channelIndex);
        const int64_t frameCount = document.getFrameCount();
        if (frameCount <= 0 || channelIndex < 0 ||
            channelIndex >= document.getChannelCount() ||
            endSampleExclusive <= 0.0 ||
            startSampleInclusive >= static_cast<double>(frameCount))
        {
            return false;
        }

        const double cacheBypassThreshold =
            static_cast<double>(WaveformCache::BASE_BLOCK_SIZE) *
            static_cast<double>(std::max<uint8_t>(1, pixelScale));
        const bool bypassCache = samplesPerPixel < cacheBypassThreshold;
        const auto &waveformCache = document.getWaveformCache(channelIndex);
        const int cacheLevel =
            bypassCache ? 0 : waveformCache.getLevelIndex(samplesPerPixel);
        const int64_t samplesPerPeak =
            bypassCache ? 0 : WaveformCache::samplesPerPeakForLevel(cacheLevel);
        const std::vector<Peak> *peaks =
            bypassCache ? nullptr : &waveformCache.getLevel(samplesPerPixel);

        auto accumulateRawPeakRange = [&](const int64_t startSample,
                                          const int64_t endSampleWindowExclusive,
                                          Peak &ioPeak,
                                          bool &ioHasPeak) -> void
        {
            if (startSample >= endSampleWindowExclusive)
            {
                return;
            }

            float minv = sampleData[startSample];
            float maxv = sampleData[startSample];
            for (int64_t i = startSample + 1; i < endSampleWindowExclusive; ++i)
            {
                const float v = sampleData[i];
                minv = std::min(minv, v);
                maxv = std::max(maxv, v);
            }

            if (!ioHasPeak)
            {
                ioPeak = {minv, maxv};
                ioHasPeak = true;
                return;
            }

            ioPeak.min = std::min(ioPeak.min, minv);
            ioPeak.max = std::max(ioPeak.max, maxv);
        };

        int64_t a = static_cast<int64_t>(std::floor(startSampleInclusive));
        int64_t b = static_cast<int64_t>(std::floor(endSampleExclusive));
        a = std::clamp<int64_t>(a, 0, frameCount - 1);
        b = std::clamp<int64_t>(b, a + 1, frameCount);

        if (bypassCache)
        {
            float minv = sampleData[a];
            float maxv = sampleData[a];
            for (int64_t i = a + 1; i < b; ++i)
            {
                const float v = sampleData[i];
                minv = std::min(minv, v);
                maxv = std::max(maxv, v);
            }
            outPeak = {minv, maxv};
            return true;
        }

        if (!peaks || peaks->empty())
        {
            return false;
        }

        Peak peak{};
        bool hasPeak = false;
        const int64_t firstFullBlockStart =
            ((a + samplesPerPeak - 1) / samplesPerPeak) * samplesPerPeak;
        const int64_t lastFullBlockEnd = (b / samplesPerPeak) * samplesPerPeak;

        accumulateRawPeakRange(a, std::min(b, firstFullBlockStart), peak, hasPeak);

        if (firstFullBlockStart < lastFullBlockEnd)
        {
            const int64_t requestedI0 = firstFullBlockStart / samplesPerPeak;
            const int64_t requestedI1Exclusive = lastFullBlockEnd / samplesPerPeak;
            const int64_t cachedI0 = std::clamp<int64_t>(
                requestedI0, 0, static_cast<int64_t>(peaks->size()));
            const int64_t cachedI1Exclusive = std::clamp<int64_t>(
                requestedI1Exclusive, 0, static_cast<int64_t>(peaks->size()));
            const int64_t cachedFullBlockStart = cachedI0 * samplesPerPeak;
            const int64_t cachedFullBlockEnd = cachedI1Exclusive * samplesPerPeak;

            accumulateRawPeakRange(firstFullBlockStart,
                                   std::min(lastFullBlockEnd, cachedFullBlockStart),
                                   peak, hasPeak);

            for (int64_t i = cachedI0; i < cachedI1Exclusive; ++i)
            {
                if (!hasPeak)
                {
                    peak = (*peaks)[i];
                    hasPeak = true;
                }
                else
                {
                    peak.min = std::min(peak.min, (*peaks)[i].min);
                    peak.max = std::max(peak.max, (*peaks)[i].max);
                }
            }

            accumulateRawPeakRange(std::max(firstFullBlockStart, cachedFullBlockEnd),
                                   lastFullBlockEnd, peak, hasPeak);
        }

        accumulateRawPeakRange(std::max(a, lastFullBlockEnd), b, peak, hasPeak);
        if (!hasPeak)
        {
            return false;
        }

        outPeak = peak;
        return true;
    }

    inline std::vector<BlockWaveformPeakColumnPlan> planWaveformOverviewPeakColumns(
        const cupuacu::Document &document, const int channelIndex,
        const int64_t sampleOffset, const double samplesPerPixel,
        const int widthToUse, const uint8_t pixelScale)
    {
        auto lookupPeak = [&](const int x, Peak &out) -> bool
        {
            double aD = 0.0;
            double bD = 0.0;
            Waveform::getBlockRenderSampleWindowForPixel(
                x, sampleOffset, samplesPerPixel, aD, bD);
            return computeWaveformPeakForSampleWindow(
                document, channelIndex, sampleOffset, samplesPerPixel, pixelScale, aD,
                bD, out);
        };

        return planBlockWaveformPeakColumns(
            widthToUse,
            Waveform::getBlockRenderPhasePixels(sampleOffset, samplesPerPixel),
            lookupPeak);
    }

    inline std::optional<SDL_Rect> planFrameSpanRect(const int64_t startFrame,
                                                     const int64_t frameCount,
                                                     const int64_t sampleOffset,
                                                     const double samplesPerPixel,
                                                     const int width,
                                                     const int height)
    {
        SDL_FRect rect{};
        if (!Waveform::computeBlockModeSelectionFillRect(
                startFrame, startFrame + frameCount, sampleOffset, samplesPerPixel,
                width, height, rect))
        {
            return std::nullopt;
        }
        return SDL_Rect{static_cast<int>(std::floor(rect.x)),
                        static_cast<int>(std::floor(rect.y)),
                        std::max(1, static_cast<int>(std::ceil(rect.w))),
                        std::max(1, static_cast<int>(std::ceil(rect.h)))};
    }
} // namespace cupuacu::gui
