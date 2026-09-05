#pragma once

#include "../DocumentSession.hpp"
#include "Waveform.hpp"
#include "WaveformBlockRenderPlanning.hpp"
#include "WaveformCache.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace cupuacu::gui
{
    inline int64_t planWaveformRenderFrameLimit(const int64_t frameCount,
                                                const double samplesPerPixel,
                                                const uint8_t pixelScale,
                                                const WaveformCache &cache,
                                                const bool cacheBuildActive)
    {
        const double rawThreshold =
            WaveformCache::BASE_BLOCK_SIZE *
            static_cast<double>(std::max<uint8_t>(1, pixelScale));
        if (cacheBuildActive && samplesPerPixel >= rawThreshold)
        {
            // Rendering an unbuilt overview must not trigger a full audio
            // scan on the UI thread. Sample-level views remain bounded.
            return std::clamp<int64_t>(cache.builtSamplePrefixEnd(), 0,
                                       frameCount);
        }
        return frameCount;
    }

    struct BackgroundBlockRenderInputPlan
    {
        bool bypassCache = true;
        int cacheLevel = 0;
        int64_t samplesPerPeak = 0;
        int64_t rawSampleStart = 0;
        int64_t rawSampleEndExclusive = 0;
    };

    inline BackgroundBlockRenderInputPlan planBackgroundBlockRenderInput(
        const int64_t frameCount, const int64_t sampleOffset,
        const double samplesPerPixel, const int widthToUse,
        const uint8_t pixelScale, const WaveformCache &waveformCache)
    {
        BackgroundBlockRenderInputPlan plan{};
        if (frameCount <= 0 || widthToUse <= 0 || samplesPerPixel <= 0.0)
        {
            return plan;
        }

        const double cacheBypassThreshold =
            static_cast<double>(WaveformCache::BASE_BLOCK_SIZE) *
            static_cast<double>(std::max<uint8_t>(1, pixelScale));
        plan.bypassCache = samplesPerPixel < cacheBypassThreshold;
        if (plan.bypassCache)
        {
            plan.rawSampleStart =
                std::clamp<int64_t>(sampleOffset, 0, frameCount);
            plan.rawSampleEndExclusive = std::clamp<int64_t>(
                sampleOffset +
                    static_cast<int64_t>(std::ceil(
                        samplesPerPixel * static_cast<double>(widthToUse + 1))),
                0, frameCount);
            return plan;
        }

        plan.cacheLevel = waveformCache.getLevelIndex(samplesPerPixel);
        plan.samplesPerPeak =
            WaveformCache::samplesPerPeakForLevel(plan.cacheLevel);
        return plan;
    }

    struct WaveformOverviewDebugStats
    {
        int64_t windowsRequested = 0;
        int64_t windowsComputed = 0;
        int64_t windowsBypassedCache = 0;
        int64_t windowsUsedCache = 0;
        int64_t rawSamplesScanned = 0;
        int64_t cachedPeaksUsed = 0;
    };

    inline bool computeWaveformPeakForSampleWindow(
        const cupuacu::DocumentSession &session, const int channelIndex,
        const int64_t sampleOffset, const double samplesPerPixel,
        const uint8_t pixelScale, const double startSampleInclusive,
        const double endSampleExclusive, Peak &outPeak,
        WaveformOverviewDebugStats *debugStats = nullptr)
    {
        const auto &document = session.document;
        if (session.openingPreview)
        {
            // Opening previews contain peaks only. Expand edge windows to base
            // peaks instead of consulting audio which has not been committed.
            if (channelIndex < 0 || channelIndex >= document.getChannelCount())
            {
                return false;
            }
            const auto &cache = session.getWaveformCache(channelIndex);
            constexpr int64_t base = WaveformCache::BASE_BLOCK_SIZE;
            const auto prefix = cache.builtSamplePrefixEnd();
            if (endSampleExclusive <= 0 || startSampleInclusive >= prefix)
            {
                return false;
            }
            int64_t position =
                static_cast<int64_t>(std::max(0.0, startSampleInclusive)) /
                base;
            const auto end = std::min<int64_t>(
                cache.validPeakCountForLevel(0),
                static_cast<int64_t>(std::ceil(
                    std::min(endSampleExclusive, static_cast<double>(prefix)) /
                    base)));
            bool found = false;
            while (position < end)
            {
                int level = cache.getLevelIndex(samplesPerPixel);
                int64_t width = int64_t{1} << level;
                while (level > 0 &&
                       (position % width != 0 || width > end - position))
                {
                    --level;
                    width /= 2;
                }
                const auto peak =
                    cache.getLevelByIndex(level)[position / width];
                if (!found)
                {
                    outPeak = peak;
                }
                else
                {
                    outPeak.min = std::min(outPeak.min, peak.min);
                    outPeak.max = std::max(outPeak.max, peak.max);
                }
                found = true;
                position += width;
                if (debugStats)
                {
                    ++debugStats->cachedPeaksUsed;
                }
            }
            if (debugStats)
            {
                ++debugStats->windowsUsedCache;
            }
            return found;
        }
        // Keep the borrowed sample view alive and use dimensions from the
        // same revision throughout the query.
        const auto buffer = document.getAudioBuffer();
        const auto sampleData = buffer->getImmutableChannelData(channelIndex);
        const int64_t frameCount = buffer->getFrameCount();
        if (frameCount <= 0 || channelIndex < 0 ||
            channelIndex >= buffer->getChannelCount() ||
            endSampleExclusive <= 0.0 ||
            startSampleInclusive >= static_cast<double>(frameCount))
        {
            return false;
        }

        const double cacheBypassThreshold =
            static_cast<double>(WaveformCache::BASE_BLOCK_SIZE) *
            static_cast<double>(std::max<uint8_t>(1, pixelScale));
        const bool bypassCache = samplesPerPixel < cacheBypassThreshold;
        const auto &waveformCache = session.getWaveformCache(channelIndex);
        const int cacheLevel =
            bypassCache ? 0 : waveformCache.getLevelIndex(samplesPerPixel);
        auto accumulateRawPeakRange = [&](const int64_t startSample,
                                          const int64_t endSampleWindowExclusive,
                                          Peak &ioPeak,
                                          bool &ioHasPeak) -> void
        {
            if (startSample >= endSampleWindowExclusive)
            {
                return;
            }
            if (debugStats)
            {
                debugStats->rawSamplesScanned +=
                    endSampleWindowExclusive - startSample;
            }

            float minv = sampleData[startSample];
            float maxv = minv;
            std::array<float, WaveformCache::BASE_BLOCK_SIZE> block;
            CUPUACU_METRIC(performance::add(
                performance::Work::SampleBytesCopied,
                (endSampleWindowExclusive - startSample - 1) * sizeof(float)));
            for (int64_t start = startSample + 1; start < endSampleWindowExclusive;
                 start += block.size())
            {
                const auto count = std::min<int64_t>(
                    block.size(), endSampleWindowExclusive - start);
                sampleData.read(start, block.data(), count);
                for (int64_t i = 0; i < count; ++i)
                {
                    minv = std::min(minv, block[i]);
                    maxv = std::max(maxv, block[i]);
                }
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
            if (debugStats)
            {
                ++debugStats->windowsBypassedCache;
            }
            bool hasPeak = false;
            accumulateRawPeakRange(a, b, outPeak, hasPeak);
            return hasPeak;
        }

        if (debugStats)
        {
            ++debugStats->windowsUsedCache;
        }

        if (waveformCache.levelsCount() == 0)
        {
            return false;
        }

        Peak peak{};
        bool hasPeak = false;
        // Use finer cached levels at the edges of a coarse display peak.
        // Raw work for a completely built cache is bounded by two base blocks,
        // independent of the zoom level or file length.
        constexpr int64_t base = WaveformCache::BASE_BLOCK_SIZE;
        const int64_t firstFullBlockStart = ((a + base - 1) / base) * base;
        const int64_t lastFullBlockEnd = (b / base) * base;
        if (firstFullBlockStart >= b)
        {
            accumulateRawPeakRange(a, b, outPeak, hasPeak);
            return hasPeak;
        }
        accumulateRawPeakRange(a, std::min(b, firstFullBlockStart), peak, hasPeak);

        int64_t position = firstFullBlockStart;
        const int64_t cachedEnd = std::min(
            lastFullBlockEnd, waveformCache.validPeakCountForLevel(0) * base);
        while (position < cachedEnd)
        {
            int level = cacheLevel;
            int64_t width = WaveformCache::samplesPerPeakForLevel(level);
            while (level > 0 &&
                   (position % width != 0 || width > cachedEnd - position))
            {
                --level;
                width /= 2;
            }
            const auto &levelPeaks = waveformCache.getLevelByIndex(level);
            const auto next = levelPeaks[position / width];
            if (debugStats)
            {
                ++debugStats->cachedPeaksUsed;
            }
            if (!hasPeak)
            {
                peak = next;
                hasPeak = true;
            }
            else
            {
                peak.min = std::min(peak.min, next.min);
                peak.max = std::max(peak.max, next.max);
            }
            position += width;
        }
        // Dirty/unbuilt cache ranges still need exact raw fallback.
        accumulateRawPeakRange(position, lastFullBlockEnd, peak, hasPeak);

        accumulateRawPeakRange(std::max(a, lastFullBlockEnd), b, peak, hasPeak);
        if (!hasPeak)
        {
            return false;
        }

        outPeak = peak;
        return true;
    }

    inline std::vector<BlockWaveformPeakColumnPlan> planWaveformOverviewPeakColumns(
        const cupuacu::DocumentSession &session, const int channelIndex,
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
                session, channelIndex, sampleOffset, samplesPerPixel, pixelScale, aD,
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
