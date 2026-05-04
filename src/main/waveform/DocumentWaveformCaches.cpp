#include "DocumentWaveformCaches.hpp"

#include "../Document.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace cupuacu::waveform
{
    DocumentWaveformCaches::~DocumentWaveformCaches()
    {
        stopBuild();
    }

    DocumentWaveformCaches::DocumentWaveformCaches(
        const DocumentWaveformCaches &other)
        : caches(other.caches)
    {
    }

    DocumentWaveformCaches &
    DocumentWaveformCaches::operator=(const DocumentWaveformCaches &other)
    {
        if (this == &other)
        {
            return *this;
        }

        caches = other.caches;
        appliedProgress.reset();
        return *this;
    }

    DocumentWaveformCaches::BuildJob::BuildJob(const Document *documentToRead,
                                               BuildRequest requestToUse)
        : document(documentToRead),
          request(std::move(requestToUse))
    {
    }

    DocumentWaveformCaches::BuildJob::~BuildJob()
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }

    void DocumentWaveformCaches::BuildJob::start()
    {
        worker = std::thread([this]
                             { run(); });
    }

    bool DocumentWaveformCaches::BuildJob::isCompleted() const
    {
        std::lock_guard lock(mutex);
        return completed;
    }

    std::vector<DocumentWaveformCaches::BuildOutput>
    DocumentWaveformCaches::BuildJob::takePublishedOutputs(
        const std::size_t maxCount)
    {
        std::lock_guard lock(mutex);
        if (maxCount == 0 || outputs.empty())
        {
            return {};
        }

        const auto count = std::min(maxCount, outputs.size());
        std::vector<BuildOutput> drained;
        drained.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            drained.push_back(std::move(outputs.front()));
            outputs.pop_front();
        }
        return drained;
    }

    bool DocumentWaveformCaches::BuildJob::hasPublishedOutputs() const
    {
        std::lock_guard lock(mutex);
        return !outputs.empty();
    }

    DocumentWaveformCaches::BuildProgress
    DocumentWaveformCaches::BuildJob::getProgress() const
    {
        std::lock_guard lock(mutex);
        return progress;
    }

    void DocumentWaveformCaches::BuildJob::run()
    {
        constexpr int64_t kBuildChunkBlocks = 4096;
        if (document && !request.channels.empty())
        {
            struct ChannelBuildRuntime
            {
                int64_t channelIndex = 0;
                gui::WaveformCache::BuildState state;
                int64_t nextBlock = 0;
                int64_t dirtyToBlock = -1;
            };

            int64_t totalBlocks = 0;
            std::vector<ChannelBuildRuntime> channels;
            channels.reserve(request.channels.size());
            for (const auto &channel : request.channels)
            {
                totalBlocks += channel.totalDirtyBlocks;
                channels.push_back(ChannelBuildRuntime{
                    .channelIndex = channel.channelIndex,
                    .state = channel.buildState,
                    .nextBlock = channel.buildState.dirtyFromBlock,
                    .dirtyToBlock = channel.buildState.dirtyToBlock,
                });
            }

            {
                std::lock_guard lock(mutex);
                progress = {.completedBlocks = 0, .totalBlocks = totalBlocks};
            }

            bool workRemaining = true;
            while (workRemaining)
            {
                workRemaining = false;
                for (auto &channel : channels)
                {
                    if (channel.dirtyToBlock < channel.nextBlock)
                    {
                        continue;
                    }
                    workRemaining = true;

                    const int64_t builtFromBlock = channel.nextBlock;
                    const int64_t builtToBlock = std::min<int64_t>(
                        channel.dirtyToBlock,
                        builtFromBlock + kBuildChunkBlocks - 1);
                    const int64_t sampleStart =
                        builtFromBlock *
                        static_cast<int64_t>(gui::WaveformCache::BASE_BLOCK_SIZE);
                    const int64_t sampleEndExclusive = std::min<int64_t>(
                        channel.state.numSamples,
                        (builtToBlock + 1) * static_cast<int64_t>(
                                                 gui::WaveformCache::BASE_BLOCK_SIZE));

                    std::vector<float> samples;
                    samples.reserve(static_cast<std::size_t>(std::max<int64_t>(
                        0, sampleEndExclusive - sampleStart)));
                    {
                        auto lease = document->acquireReadLease();
                        for (int64_t sample = sampleStart;
                             sample < sampleEndExclusive; ++sample)
                        {
                            samples.push_back(
                                lease.getSample(channel.channelIndex, sample));
                        }
                    }

                    gui::WaveformCache::rebuildDirtyBlockRangeFromSlice(
                        channel.state.levels, channel.state.numSamples,
                        builtFromBlock, builtToBlock, sampleStart,
                        samples.data(), static_cast<int64_t>(samples.size()));

                    BuildOutput chunkOutput{
                        .waveformDataVersion = request.waveformDataVersion,
                        .completedBlocks = 0,
                        .totalBlocks = totalBlocks,
                        .completed = false,
                        .channelChunks = {BuildOutput::ChannelChunk{
                            .channelIndex = channel.channelIndex,
                            .builtFromBlock = builtFromBlock,
                            .builtToBlock = builtToBlock,
                            .levelUpdates = {},
                        }},
                    };

                    int64_t levelFrom = builtFromBlock;
                    int64_t levelTo = builtToBlock;
                    for (int level = 0;
                         level < static_cast<int>(channel.state.levels.size()) &&
                         levelTo >= levelFrom;
                         ++level)
                    {
                        auto &levelData =
                            channel.state.levels[static_cast<std::size_t>(level)];
                        const int64_t clampedFrom = std::clamp<int64_t>(
                            levelFrom, 0,
                            static_cast<int64_t>(levelData.size()));
                        const int64_t clampedTo = std::clamp<int64_t>(
                            levelTo, -1,
                            static_cast<int64_t>(levelData.size()) - 1);
                        if (clampedTo >= clampedFrom)
                        {
                            auto &levelUpdates =
                                chunkOutput.channelChunks[0].levelUpdates;
                            levelUpdates.push_back(
                                gui::WaveformCache::LevelSpanUpdate{
                                    .level = level,
                                    .fromIndex = clampedFrom,
                                    .peaks = std::vector<gui::Peak>(
                                        levelData.begin() + clampedFrom,
                                        levelData.begin() + clampedTo + 1),
                                });
                        }
                        levelFrom /= 2;
                        levelTo /= 2;
                    }

                    {
                        std::lock_guard lock(mutex);
                        progress = {.completedBlocks =
                                        progress.completedBlocks +
                                        (builtToBlock - builtFromBlock + 1),
                                    .totalBlocks = totalBlocks};
                        chunkOutput.completedBlocks = progress.completedBlocks;
                        outputs.push_back(std::move(chunkOutput));
                    }

                    channel.nextBlock = builtToBlock + 1;
                }
            }
        }

        std::lock_guard lock(mutex);
        completed = true;
        outputs.push_back(BuildOutput{
            .completedBlocks = progress.completedBlocks,
            .totalBlocks = progress.totalBlocks,
            .completed = true,
        });
    }

    void DocumentWaveformCaches::stopBuild()
    {
        buildJob.reset();
        appliedProgress.reset();
    }

    void DocumentWaveformCaches::syncToChannelCount(const int64_t channelCount)
    {
        caches.resize(static_cast<std::size_t>(channelCount));
    }

    void DocumentWaveformCaches::resetToChannelCount(const int64_t channelCount)
    {
        caches.assign(static_cast<std::size_t>(channelCount),
                      gui::WaveformCache{});
    }

    gui::WaveformCache &DocumentWaveformCaches::getCache(const int channel)
    {
        return caches[static_cast<std::size_t>(channel)];
    }

    const gui::WaveformCache &
    DocumentWaveformCaches::getCache(const int channel) const
    {
        return caches[static_cast<std::size_t>(channel)];
    }

    void DocumentWaveformCaches::applyInsert(const int64_t frameIndex,
                                             const int64_t numFrames)
    {
        for (auto &cache : caches)
        {
            cache.applyInsert(frameIndex, numFrames);
        }
    }

    void DocumentWaveformCaches::applyErase(const int64_t frameIndex,
                                            const int64_t numFrames)
    {
        for (auto &cache : caches)
        {
            cache.applyErase(frameIndex, frameIndex + numFrames);
        }
    }

    void DocumentWaveformCaches::invalidateSamples(const int64_t startSample,
                                                   const int64_t endSample)
    {
        for (auto &cache : caches)
        {
            cache.invalidateSamples(startSample, endSample);
        }
    }

    bool DocumentWaveformCaches::level0SizeMatches(const int64_t channel,
                                                   const int64_t frameCount) const
    {
        if (channel < 0 || channel >= static_cast<int64_t>(caches.size()))
        {
            return false;
        }

        const auto expectedLevel0Size =
            frameCount <= 0
                ? 0
                : (frameCount + gui::WaveformCache::BASE_BLOCK_SIZE - 1) /
                      gui::WaveformCache::BASE_BLOCK_SIZE;
        const auto &cache = caches[static_cast<std::size_t>(channel)];
        return cache.levelsCount() > 0 &&
               static_cast<int64_t>(cache.getLevelByIndex(0).size()) ==
                   expectedLevel0Size;
    }

    bool DocumentWaveformCaches::needsBuild(const int64_t frameCount,
                                            const int64_t channelCount) const
    {
        if (frameCount <= 0 || channelCount <= 0)
        {
            return false;
        }

        for (int64_t channel = 0; channel < channelCount; ++channel)
        {
            const auto &cache = caches[static_cast<std::size_t>(channel)];
            if (!level0SizeMatches(channel, frameCount) || cache.hasDirtyBlocks())
            {
                return true;
            }
        }

        return false;
    }

    int64_t DocumentWaveformCaches::totalDirtyBlocks(const int64_t frameCount,
                                                     const int64_t channelCount) const
    {
        if (frameCount <= 0 || channelCount <= 0)
        {
            return 0;
        }

        int64_t total = 0;
        for (int64_t channel = 0; channel < channelCount; ++channel)
        {
            auto buildState =
                caches[static_cast<std::size_t>(channel)].snapshotBuildState();
            if (!level0SizeMatches(channel, frameCount))
            {
                buildState = gui::WaveformCache::makeFullBuildState(frameCount);
            }
            total += gui::WaveformCache::dirtyBlockCount(buildState);
        }

        return total;
    }

    void DocumentWaveformCaches::clear()
    {
        for (auto &cache : caches)
        {
            cache.clear();
        }
    }

    std::optional<DocumentWaveformCaches::BuildRequest>
    DocumentWaveformCaches::makeBuildRequest(const int64_t frameCount,
                                             const int64_t channelCount,
                                             const uint64_t waveformDataVersion) const
    {
        if (frameCount <= 0 || channelCount <= 0)
        {
            return std::nullopt;
        }

        BuildRequest request;
        request.waveformDataVersion = waveformDataVersion;
        request.channels.reserve(static_cast<std::size_t>(channelCount));
        for (int64_t channel = 0; channel < channelCount; ++channel)
        {
            auto buildState =
                caches[static_cast<std::size_t>(channel)].snapshotBuildState();
            if (!level0SizeMatches(channel, frameCount))
            {
                buildState = gui::WaveformCache::makeFullBuildState(frameCount);
            }
            const auto totalDirtyBlocks =
                gui::WaveformCache::dirtyBlockCount(buildState);

            request.channels.push_back(
                {.channelIndex = channel,
                 .buildState = std::move(buildState),
                 .totalDirtyBlocks = totalDirtyBlocks});
        }

        return request;
    }

    void DocumentWaveformCaches::startBuild(const Document &document,
                                            const int64_t frameCount,
                                            const int64_t channelCount,
                                            const uint64_t waveformDataVersion)
    {
        if (buildJob || !needsBuild(frameCount, channelCount))
        {
            return;
        }

        auto request =
            makeBuildRequest(frameCount, channelCount, waveformDataVersion);
        if (!request.has_value())
        {
            return;
        }

        appliedProgress = {
            .completedBlocks = 0,
            .totalBlocks = totalDirtyBlocks(frameCount, channelCount),
        };
        buildJob = std::make_unique<BuildJob>(&document, std::move(*request));
        buildJob->start();
    }

    void DocumentWaveformCaches::update(const Document &document,
                                        const uint64_t waveformDataVersion)
    {
        (void)pumpWork(document, waveformDataVersion);
    }

    bool DocumentWaveformCaches::pumpWork(const Document &document,
                                          const uint64_t waveformDataVersion)
    {
        constexpr std::size_t kMaxPublishedOutputsPerPump = 256;

        bool applied = false;
        bool stateChanged = false;
        std::vector<BuildOutput> publishedOutputs;
        bool buildCompleted = false;
        bool publishedOutputsRemain = false;
        if (buildJob)
        {
            publishedOutputs = buildJob->takePublishedOutputs(
                kMaxPublishedOutputsPerPump);
            buildCompleted = buildJob->isCompleted();
            publishedOutputsRemain = buildJob->hasPublishedOutputs();
        }
        stateChanged = !publishedOutputs.empty();

        const int64_t frameCount = document.getFrameCount();
        const int64_t channelCount = document.getChannelCount();

        for (auto &output : publishedOutputs)
        {
            if (output.completed)
            {
                stateChanged = true;
                if (buildJob && buildJob->isCompleted())
                {
                    publishedOutputsRemain = buildJob->hasPublishedOutputs();
                    if (!publishedOutputsRemain)
                    {
                        buildJob.reset();
                        appliedProgress.reset();
                    }
                }
                continue;
            }

            if (output.waveformDataVersion != waveformDataVersion)
            {
                continue;
            }

            for (const auto &channelChunk : output.channelChunks)
            {
                if (channelChunk.channelIndex < 0 ||
                    channelChunk.channelIndex >=
                        static_cast<int64_t>(caches.size()))
                {
                    continue;
                }

                caches[static_cast<std::size_t>(channelChunk.channelIndex)]
                    .applyLevelSpanUpdates(frameCount, channelChunk.builtFromBlock,
                                           channelChunk.builtToBlock,
                                           channelChunk.levelUpdates);
                applied = true;
            }

            if (appliedProgress.has_value())
            {
                appliedProgress = {
                    .completedBlocks = std::max<int64_t>(
                        appliedProgress->completedBlocks,
                        output.completedBlocks),
                    .totalBlocks = std::max<int64_t>(
                        appliedProgress->totalBlocks,
                        output.totalBlocks),
                };
            }
            else
            {
                appliedProgress = {
                    .completedBlocks = output.completedBlocks,
                    .totalBlocks = output.totalBlocks,
                };
            }
        }

        if (buildCompleted && !publishedOutputsRemain)
        {
            stateChanged = true;
            if (buildJob && buildJob->isCompleted())
            {
                buildJob.reset();
                appliedProgress.reset();
            }
        }

        if (frameCount <= 0 || channelCount <= 0)
        {
            clear();
            return applied;
        }

        startBuild(document, frameCount, channelCount, waveformDataVersion);
        return applied || stateChanged;
    }

    std::optional<DocumentWaveformCaches::BuildProgress>
    DocumentWaveformCaches::getBuildProgress(const Document &document,
                                            const uint64_t waveformDataVersion) const
    {
        (void)waveformDataVersion;
        if (!buildJob)
        {
            return std::nullopt;
        }

        auto progress = appliedProgress.value_or(buildJob->getProgress());
        if (progress.totalBlocks <= 0)
        {
            progress.completedBlocks = 0;
            progress.totalBlocks =
                totalDirtyBlocks(document.getFrameCount(),
                                 document.getChannelCount());
        }
        return progress;
    }

    void DocumentWaveformCaches::rebuildSynchronously(const Document &document)
    {
        stopBuild();

        const int64_t frameCount = document.getFrameCount();
        const int64_t channelCount = document.getChannelCount();
        if (frameCount <= 0 || channelCount <= 0)
        {
            clear();
            return;
        }

        for (int64_t channel = 0; channel < channelCount; ++channel)
        {
            auto buildState =
                caches[static_cast<std::size_t>(channel)].snapshotBuildState();
            if (!level0SizeMatches(channel, frameCount))
            {
                buildState = gui::WaveformCache::makeFullBuildState(frameCount);
            }

            std::vector<float> samples;
            samples.reserve(static_cast<std::size_t>(frameCount));
            {
                auto lease = document.acquireReadLease();
                for (int64_t sample = 0; sample < frameCount; ++sample)
                {
                    samples.push_back(lease.getSample(channel, sample));
                }
            }
            auto result = gui::WaveformCache::buildFromState(buildState,
                                                             samples.data());
            caches[static_cast<std::size_t>(channel)].applyBuildResult(
                std::move(result));
        }
    }
} // namespace cupuacu::waveform
