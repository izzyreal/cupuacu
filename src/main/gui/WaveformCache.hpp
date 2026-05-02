#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace cupuacu::gui
{

    struct Peak
    {
        float min;
        float max;
    };

    class WaveformCache
    {
    public:
        constexpr static int BASE_BLOCK_SIZE = 32;
        constexpr static int MAX_LEVEL_COUNT = 16;

        struct LevelSpanUpdate
        {
            int level = 0;
            int64_t fromIndex = 0;
            std::vector<Peak> peaks;
        };

        struct BuildState
        {
            int64_t numSamples = 0;
            int64_t dirtyFromBlock = INT64_MAX;
            int64_t dirtyToBlock = -1;
            std::vector<std::vector<Peak>> levels;
        };

        struct BuildResult
        {
            int64_t numSamples = 0;
            int64_t dirtyFromBlock = INT64_MAX;
            int64_t dirtyToBlock = -1;
            std::vector<std::vector<Peak>> levels;
        };

        WaveformCache()
            : numSamples(0), dirtyFromBlock(INT64_MAX), dirtyToBlock(-1)
        {
        }

        void clear()
        {
            levels.clear();
            numSamples = 0;
            dirtyFromBlock = INT64_MAX;
            dirtyToBlock = -1;
        }

        void init(const int64_t n)
        {
            numSamples = std::max<int64_t>(0, n);
            buildStorage();
            markAllDirty();
        }

        void rebuildAll(const float *samples, const int64_t n)
        {
            numSamples = std::max<int64_t>(0, n);
            buildStorage();
            dirtyFromBlock = 0;
            dirtyToBlock = level0Size() - 1;
            rebuildDirty(samples);
        }

        void invalidateSamples(int64_t startSample, int64_t endSample)
        {
            if (endSample < startSample)
            {
                std::swap(startSample, endSample);
            }
            if (levels.empty() || numSamples <= 0)
            {
                return;
            }

            startSample = std::clamp<int64_t>(startSample, 0, numSamples - 1);
            endSample = std::clamp<int64_t>(endSample, 0, numSamples - 1);

            const int64_t b0 = startSample / BASE_BLOCK_SIZE;
            const int64_t b1 = endSample / BASE_BLOCK_SIZE;
            markDirtyBlocks(b0, b1);
        }

        void invalidateSample(const int64_t sample)
        {
            invalidateSamples(sample, sample);
        }

        int getLevelIndex(const double samplesPerPixel) const
        {
            if (levels.empty())
            {
                return 0;
            }
            const double eff =
                std::max(1.0, samplesPerPixel / (double)BASE_BLOCK_SIZE);
            const int level = (int)std::floor(std::log2(eff));
            return std::clamp(level, 0, (int)levels.size() - 1);
        }

        static int64_t samplesPerPeakForLevel(int level)
        {
            level = std::clamp(level, 0, MAX_LEVEL_COUNT - 1);
            return (int64_t)BASE_BLOCK_SIZE << level;
        }

        const std::vector<Peak> &getLevelByIndex(int level) const
        {
            if (levels.empty())
            {
                return empty;
            }
            level = std::clamp(level, 0, (int)levels.size() - 1);
            return levels[level];
        }

        void applyInsert(int64_t posSample, const int64_t countSamples)
        {
            if (countSamples <= 0)
            {
                return;
            }

            posSample = std::clamp<int64_t>(posSample, 0, numSamples);
            numSamples += countSamples;

            if (levels.empty())
            {
                buildStorage();
            }
            else
            {
                const int64_t oldL0 = (int64_t)levels[0].size();
                const int64_t newL0 = level0Size();

                levels[0].resize(newL0);

                const int64_t insertBlock = posSample / BASE_BLOCK_SIZE;
                const int64_t shift = newL0 - oldL0;
                if (shift > 0 && insertBlock < oldL0)
                {
                    std::move_backward(levels[0].begin() + insertBlock,
                                       levels[0].begin() + oldL0,
                                       levels[0].begin() + oldL0 + shift);
                }

                for (int l = 1; l < (int)levels.size(); ++l)
                {
                    levels[l].resize(levelSize(l));
                }
            }

            dirtyFromBlock =
                std::min<int64_t>(dirtyFromBlock, posSample / BASE_BLOCK_SIZE);
            dirtyToBlock = level0Size() - 1;
        }

        void applyErase(int64_t startSample, int64_t endSample)
        {
            if (endSample < startSample)
            {
                std::swap(startSample, endSample);
            }
            if (numSamples <= 0)
            {
                return;
            }

            startSample = std::clamp<int64_t>(startSample, 0, numSamples);
            endSample = std::clamp<int64_t>(endSample, 0, numSamples);
            if (endSample <= startSample)
            {
                return;
            }

            if (levels.empty())
            {
                numSamples = std::max<int64_t>(
                    0, numSamples - (endSample - startSample));
                buildStorage();
                markAllDirty();
                return;
            }

            const int64_t eraseCount = endSample - startSample;

            const int64_t oldL0 = (int64_t)levels[0].size();

            const int64_t b0 = startSample / BASE_BLOCK_SIZE;
            const int64_t b1 = (endSample - 1) / BASE_BLOCK_SIZE;
            const int64_t tailStart = b1 + 1;

            numSamples = std::max<int64_t>(0, numSamples - eraseCount);
            const int64_t newL0 = level0Size();

            if (oldL0 > 0)
            {
                const int64_t dst = std::min<int64_t>(b0, oldL0);
                const int64_t src = std::min<int64_t>(tailStart, oldL0);
                if (src < oldL0 && dst < oldL0)
                {
                    std::move(levels[0].begin() + src,
                              levels[0].begin() + oldL0,
                              levels[0].begin() + dst);
                }
                levels[0].resize(newL0);
            }
            else
            {
                levels[0].resize(newL0);
            }

            for (int l = 1; l < (int)levels.size(); ++l)
            {
                levels[l].resize(levelSize(l));
            }

            dirtyFromBlock = std::min<int64_t>(dirtyFromBlock, b0);
            dirtyToBlock = level0Size() - 1;
        }

        const std::vector<Peak> &getLevel(const double samplesPerPixel) const
        {
            return getLevelByIndex(getLevelIndex(samplesPerPixel));
        }

        int levelsCount() const
        {
            return (int)levels.size();
        }

        [[nodiscard]] bool hasDirtyBlocks() const
        {
            return dirtyToBlock >= dirtyFromBlock;
        }

        [[nodiscard]] BuildState snapshotBuildState() const
        {
            return {
                .numSamples = numSamples,
                .dirtyFromBlock = dirtyFromBlock,
                .dirtyToBlock = dirtyToBlock,
                .levels = levels,
            };
        }

        [[nodiscard]] static BuildResult buildFromState(
            const BuildState &state,
            const float *samples)
        {
            BuildResult result{
                .numSamples = state.numSamples,
                .dirtyFromBlock = state.dirtyFromBlock,
                .dirtyToBlock = state.dirtyToBlock,
                .levels = state.levels,
            };
            rebuildDirtyLevels(result.levels, result.numSamples,
                               result.dirtyFromBlock, result.dirtyToBlock,
                               samples);
            return result;
        }

        [[nodiscard]] static BuildState makeFullBuildState(
            const int64_t numSamplesToUse)
        {
            WaveformCache cache;
            cache.init(numSamplesToUse);
            return cache.snapshotBuildState();
        }

        [[nodiscard]] static int64_t dirtyBlockCount(const BuildState &state)
        {
            if (state.dirtyToBlock < state.dirtyFromBlock)
            {
                return 0;
            }
            return state.dirtyToBlock - state.dirtyFromBlock + 1;
        }

        void applyBuildResult(BuildResult result)
        {
            numSamples = result.numSamples;
            dirtyFromBlock = result.dirtyFromBlock;
            dirtyToBlock = result.dirtyToBlock;
            levels = std::move(result.levels);
        }

        static void rebuildDirtyBlockRange(std::vector<std::vector<Peak>> &levelsToUse,
                                           const int64_t numSamplesToUse,
                                           const int64_t fromBlock,
                                           const int64_t toBlock,
                                           const float *samples)
        {
            int64_t dirtyFromBlockToUse = fromBlock;
            int64_t dirtyToBlockToUse = toBlock;
            rebuildDirtyLevels(levelsToUse, numSamplesToUse, dirtyFromBlockToUse,
                               dirtyToBlockToUse, samples);
        }

        static void rebuildDirtyBlockRangeFromSlice(
            std::vector<std::vector<Peak>> &levelsToUse,
            const int64_t numSamplesToUse, const int64_t fromBlock,
            const int64_t toBlock, const int64_t sampleBaseIndex,
            const float *samples, const int64_t samplesCount)
        {
            if (levelsToUse.empty() || numSamplesToUse <= 0 ||
                fromBlock > toBlock)
            {
                return;
            }

            const int64_t max0 = level0SizeFromSamples(numSamplesToUse) - 1;
            if (max0 < 0)
            {
                return;
            }

            const int64_t from0 = std::clamp<int64_t>(fromBlock, 0, max0);
            const int64_t to0 = std::clamp<int64_t>(toBlock, 0, max0);

            for (int64_t blk = from0; blk <= to0; ++blk)
            {
                const int64_t s0 = blk * static_cast<int64_t>(BASE_BLOCK_SIZE);
                const int64_t s1 =
                    std::min<int64_t>(s0 + BASE_BLOCK_SIZE, numSamplesToUse);
                if (s0 >= s1)
                {
                    levelsToUse[0][blk] = {0.0f, 0.0f};
                    continue;
                }

                const int64_t localS0 = s0 - sampleBaseIndex;
                const int64_t localS1 = s1 - sampleBaseIndex;
                if (localS0 < 0 || localS1 > samplesCount)
                {
                    continue;
                }

                float minv = samples[localS0];
                float maxv = minv;
                for (int64_t i = localS0 + 1; i < localS1; ++i)
                {
                    const float v = samples[i];
                    minv = std::min(minv, v);
                    maxv = std::max(maxv, v);
                }
                levelsToUse[0][blk] = {minv, maxv};
            }

            int64_t pFrom = from0;
            int64_t pTo = to0;
            for (int level = 1; level < static_cast<int>(levelsToUse.size());
                 ++level)
            {
                auto &prev = levelsToUse[static_cast<std::size_t>(level - 1)];
                auto &cur = levelsToUse[static_cast<std::size_t>(level)];
                if (cur.empty())
                {
                    break;
                }

                int64_t cFrom = std::clamp<int64_t>(
                    pFrom / 2, 0, static_cast<int64_t>(cur.size()) - 1);
                int64_t cTo = std::clamp<int64_t>(
                    pTo / 2, 0, static_cast<int64_t>(cur.size()) - 1);
                for (int64_t i = cFrom; i <= cTo; ++i)
                {
                    const int64_t a = i * 2;
                    const int64_t b = a + 1;

                    float minv = prev[a].min;
                    float maxv = prev[a].max;
                    if (b < static_cast<int64_t>(prev.size()))
                    {
                        minv = std::min(minv, prev[b].min);
                        maxv = std::max(maxv, prev[b].max);
                    }
                    cur[i] = {minv, maxv};
                }

                pFrom = cFrom;
                pTo = cTo;
                if (static_cast<int64_t>(cur.size()) <= 1)
                {
                    break;
                }
            }
        }

        void applyLevelSpanUpdates(const int64_t numSamplesToUse,
                                   const int64_t builtFromBlock,
                                   const int64_t builtToBlock,
                                   const std::vector<LevelSpanUpdate> &updates)
        {
            numSamples = std::max<int64_t>(0, numSamplesToUse);
            const int64_t expectedLevel0Size = level0SizeFromSamples(numSamples);
            if (levels.empty() ||
                static_cast<int64_t>(levels.front().size()) != expectedLevel0Size)
            {
                buildStorage();
                markAllDirty();
            }

            for (const auto &update : updates)
            {
                if (update.level < 0 ||
                    update.level >= static_cast<int>(levels.size()) ||
                    update.peaks.empty())
                {
                    continue;
                }

                auto &level = levels[static_cast<std::size_t>(update.level)];
                const int64_t fromIndex =
                    std::clamp<int64_t>(update.fromIndex, 0,
                                        static_cast<int64_t>(level.size()));
                const int64_t maxWritable =
                    static_cast<int64_t>(level.size()) - fromIndex;
                const int64_t count = std::clamp<int64_t>(
                    static_cast<int64_t>(update.peaks.size()), 0, maxWritable);
                for (int64_t i = 0; i < count; ++i)
                {
                    level[static_cast<std::size_t>(fromIndex + i)] =
                        update.peaks[static_cast<std::size_t>(i)];
                }
            }

            if (dirtyToBlock < dirtyFromBlock)
            {
                return;
            }

            if (builtFromBlock <= dirtyFromBlock)
            {
                dirtyFromBlock = std::min<int64_t>(dirtyToBlock + 1,
                                                   builtToBlock + 1);
            }
            if (dirtyFromBlock > dirtyToBlock)
            {
                dirtyFromBlock = INT64_MAX;
                dirtyToBlock = -1;
            }
        }

        void rebuildDirty(const float *samples)
        {
            auto result = buildFromState(snapshotBuildState(), samples);
            applyBuildResult(std::move(result));
        }

    private:
        static void rebuildDirtyLevels(std::vector<std::vector<Peak>> &levelsToUse,
                                       const int64_t numSamplesToUse,
                                       int64_t &dirtyFromBlockToUse,
                                       int64_t &dirtyToBlockToUse,
                                       const float *samples)
        {
            if (levelsToUse.empty() || numSamplesToUse <= 0)
            {
                dirtyFromBlockToUse = INT64_MAX;
                dirtyToBlockToUse = -1;
                return;
            }
            if (dirtyToBlockToUse < dirtyFromBlockToUse)
            {
                return;
            }

            const int64_t max0 = level0SizeFromSamples(numSamplesToUse) - 1;
            if (max0 < 0)
            {
                dirtyFromBlockToUse = INT64_MAX;
                dirtyToBlockToUse = -1;
                return;
            }

            const int64_t from0 =
                std::clamp<int64_t>(dirtyFromBlockToUse, 0, max0);
            const int64_t to0 = std::clamp<int64_t>(dirtyToBlockToUse, 0, max0);

            for (int64_t blk = from0; blk <= to0; ++blk)
            {
                const int64_t s0 = blk * (int64_t)BASE_BLOCK_SIZE;
                const int64_t s1 =
                    std::min<int64_t>(s0 + BASE_BLOCK_SIZE, numSamplesToUse);
                if (s0 >= s1)
                {
                    levelsToUse[0][blk] = {0.0f, 0.0f};
                    continue;
                }

                float minv = samples[s0];
                float maxv = samples[s0];
                for (int64_t i = s0 + 1; i < s1; ++i)
                {
                    const float v = samples[i];
                    minv = std::min(minv, v);
                    maxv = std::max(maxv, v);
                }
                levelsToUse[0][blk] = {minv, maxv};
            }

            int64_t pFrom = from0;
            int64_t pTo = to0;

            for (int l = 1; l < (int)levelsToUse.size(); ++l)
            {
                auto &prev = levelsToUse[l - 1];
                auto &cur = levelsToUse[l];

                if (cur.empty())
                {
                    break;
                }

                int64_t cFrom = pFrom / 2;
                int64_t cTo = pTo / 2;

                cFrom = std::clamp<int64_t>(cFrom, 0, (int64_t)cur.size() - 1);
                cTo = std::clamp<int64_t>(cTo, 0, (int64_t)cur.size() - 1);

                for (int64_t i = cFrom; i <= cTo; ++i)
                {
                    const int64_t a = i * 2;
                    const int64_t b = a + 1;

                    float minv = prev[a].min;
                    float maxv = prev[a].max;
                    if (b < (int64_t)prev.size())
                    {
                        minv = std::min(minv, prev[b].min);
                        maxv = std::max(maxv, prev[b].max);
                    }
                    cur[i] = {minv, maxv};
                }

                pFrom = cFrom;
                pTo = cTo;
                if ((int64_t)cur.size() <= 1)
                {
                    break;
                }
            }

            dirtyFromBlockToUse = INT64_MAX;
            dirtyToBlockToUse = -1;
        }

        void buildStorage()
        {
            levels.clear();
            levels.resize(MAX_LEVEL_COUNT);

            const int64_t sz0 = level0Size();
            levels[0].resize(sz0);

            for (int l = 1; l < MAX_LEVEL_COUNT; ++l)
            {
                const int64_t prev = (int64_t)levels[l - 1].size();
                const int64_t cur = (prev + 1) / 2;
                levels[l].resize(cur);
                if (cur <= 1)
                {
                    levels.resize(l + 1);
                    break;
                }
            }
        }

        static int64_t level0SizeFromSamples(const int64_t ns)
        {
            if (ns <= 0)
            {
                return 0;
            }
            return (ns + BASE_BLOCK_SIZE - 1) / BASE_BLOCK_SIZE;
        }

        int64_t level0Size() const
        {
            return level0SizeFromSamples(numSamples);
        }

        int64_t levelSize(const int level) const
        {
            int64_t sz = level0Size();
            for (int l = 1; l <= level; ++l)
            {
                sz = (sz + 1) / 2;
            }
            return sz;
        }

        void markAllDirty()
        {
            dirtyFromBlock = 0;
            dirtyToBlock = level0Size() - 1;
        }

        void markDirtyBlocks(int64_t from0, int64_t to0)
        {
            if (levels.empty())
            {
                return;
            }
            const int64_t max0 = level0Size() - 1;
            if (max0 < 0)
            {
                return;
            }

            from0 = std::clamp<int64_t>(from0, 0, max0);
            to0 = std::clamp<int64_t>(to0, 0, max0);

            dirtyFromBlock = std::min(dirtyFromBlock, from0);
            dirtyToBlock = std::max(dirtyToBlock, to0);
        }

    private:
        int64_t numSamples;
        std::vector<std::vector<Peak>> levels;
        int64_t dirtyFromBlock;
        int64_t dirtyToBlock;
        inline static const std::vector<Peak> empty;
    };

} // namespace cupuacu::gui
