#include "Document.hpp"

#include "audio/PreservationTrackingAudioBuffer.hpp"

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <shared_mutex>
#include <utility>

namespace cupuacu
{
    namespace
    {
        uint64_t nextPreservationSourceId()
        {
            static uint64_t nextId = 1;
            return nextId++;
        }
    } // namespace

    Document::WaveformCacheBuildJob::WaveformCacheBuildJob(
        const Document *documentToRead)
        : document(documentToRead)
    {
    }

    Document::WaveformCacheBuildJob::~WaveformCacheBuildJob()
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }

    void Document::WaveformCacheBuildJob::start()
    {
        worker = std::thread([this]
                             { run(); });
    }

    bool Document::WaveformCacheBuildJob::isCompleted() const
    {
        std::lock_guard lock(mutex);
        return completed;
    }

    std::vector<Document::WaveformCacheBuildOutput>
    Document::WaveformCacheBuildJob::takePublishedOutputs(const std::size_t maxCount)
    {
        std::lock_guard lock(mutex);
        if (maxCount == 0 || outputs.empty())
        {
            return {};
        }

        const auto count = std::min(maxCount, outputs.size());
        std::vector<WaveformCacheBuildOutput> drained;
        drained.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            drained.push_back(std::move(outputs.front()));
            outputs.pop_front();
        }
        return drained;
    }

    bool Document::WaveformCacheBuildJob::hasPublishedOutputs() const
    {
        std::lock_guard lock(mutex);
        return !outputs.empty();
    }

    Document::WaveformCacheBuildProgress
    Document::WaveformCacheBuildJob::getProgress() const
    {
        std::lock_guard lock(mutex);
        return progress;
    }

    void Document::WaveformCacheBuildJob::run()
    {
        constexpr int64_t kBuildChunkBlocks = 4096;
        if (document)
        {
            if (const auto request = document->captureWaveformCacheBuildRequest();
                request.has_value())
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
                channels.reserve(request->channels.size());
                for (const auto &channel : request->channels)
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
                            (builtToBlock + 1) *
                                static_cast<int64_t>(
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
                            samples.data(),
                            static_cast<int64_t>(samples.size()));

                        WaveformCacheBuildOutput chunkOutput{
                            .waveformDataVersion = request->waveformDataVersion,
                            .completedBlocks = 0,
                            .totalBlocks = totalBlocks,
                            .completed = false,
                            .channelChunks =
                                {WaveformCacheBuildOutput::ChannelChunk{
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
                                        .peaks =
                                            std::vector<gui::Peak>(
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
        }

        std::lock_guard lock(mutex);
        completed = true;
        outputs.push_back(WaveformCacheBuildOutput{
            .completedBlocks = progress.completedBlocks,
            .totalBlocks = progress.totalBlocks,
            .completed = true,
        });
    }

    Document::~Document()
    {
        stopWaveformCacheBuild();
    }

    Document::Document(const Document &other)
    {
        std::shared_lock lock(other.dataMutex);
        buffer = other.buffer;
        sampleRate = other.sampleRate;
        format = other.format;
        preservationSourceId = other.preservationSourceId;
        waveformDataVersion = other.waveformDataVersion;
        markerDataVersion = other.markerDataVersion;
        nextMarkerId = other.nextMarkerId;
        waveformCache = other.waveformCache;
        markers = other.markers;
    }

    Document &Document::operator=(const Document &other)
    {
        if (this == &other)
        {
            return *this;
        }

        stopWaveformCacheBuild();
        std::unique_lock thisLock(dataMutex, std::defer_lock);
        std::shared_lock otherLock(other.dataMutex, std::defer_lock);
        std::lock(thisLock, otherLock);
        buffer = other.buffer;
        sampleRate = other.sampleRate;
        format = other.format;
        preservationSourceId = other.preservationSourceId;
        waveformDataVersion = other.waveformDataVersion;
        markerDataVersion = other.markerDataVersion;
        nextMarkerId = other.nextMarkerId;
        waveformCache = other.waveformCache;
        markers = other.markers;
        return *this;
    }

    Document::Document(Document &&other) noexcept
    {
        other.stopWaveformCacheBuild();
        std::unique_lock lock(other.dataMutex);
        buffer = std::move(other.buffer);
        sampleRate = other.sampleRate;
        format = other.format;
        preservationSourceId = other.preservationSourceId;
        waveformDataVersion = other.waveformDataVersion;
        markerDataVersion = other.markerDataVersion;
        nextMarkerId = other.nextMarkerId;
        waveformCache = std::move(other.waveformCache);
        markers = std::move(other.markers);
    }

    Document &Document::operator=(Document &&other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        stopWaveformCacheBuild();
        other.stopWaveformCacheBuild();
        std::unique_lock thisLock(dataMutex, std::defer_lock);
        std::unique_lock otherLock(other.dataMutex, std::defer_lock);
        std::lock(thisLock, otherLock);
        buffer = std::move(other.buffer);
        sampleRate = other.sampleRate;
        format = other.format;
        preservationSourceId = other.preservationSourceId;
        waveformDataVersion = other.waveformDataVersion;
        markerDataVersion = other.markerDataVersion;
        nextMarkerId = other.nextMarkerId;
        waveformCache = std::move(other.waveformCache);
        markers = std::move(other.markers);
        return *this;
    }

    bool Document::waveformCacheLevel0SizeMatchesUnlocked(
        const int64_t channel, const int64_t frameCount) const
    {
        if (channel < 0 || channel >= getChannelCountUnlocked())
        {
            return false;
        }

        const auto expectedLevel0Size =
            frameCount <= 0
                ? 0
                : (frameCount + gui::WaveformCache::BASE_BLOCK_SIZE - 1) /
                      gui::WaveformCache::BASE_BLOCK_SIZE;
        const auto &cache = waveformCache[static_cast<std::size_t>(channel)];
        return cache.levelsCount() > 0 &&
               static_cast<int64_t>(cache.getLevelByIndex(0).size()) ==
                   expectedLevel0Size;
    }

    bool Document::needsWaveformCacheBuildUnlocked() const
    {
        const auto frameCount = getFrameCountUnlocked();
        const auto channelCount = getChannelCountUnlocked();
        if (frameCount <= 0 || channelCount <= 0)
        {
            return false;
        }

        for (int64_t channel = 0; channel < channelCount; ++channel)
        {
            const auto &cache = waveformCache[static_cast<std::size_t>(channel)];
            if (!waveformCacheLevel0SizeMatchesUnlocked(channel, frameCount) ||
                cache.hasDirtyBlocks())
            {
                return true;
            }
        }

        return false;
    }

    int64_t Document::totalWaveformCacheDirtyBlocksUnlocked() const
    {
        const auto frameCount = getFrameCountUnlocked();
        const auto channelCount = getChannelCountUnlocked();
        if (frameCount <= 0 || channelCount <= 0)
        {
            return 0;
        }

        int64_t totalDirtyBlocks = 0;
        for (int64_t channel = 0; channel < channelCount; ++channel)
        {
            auto buildState =
                waveformCache[static_cast<std::size_t>(channel)].snapshotBuildState();
            if (!waveformCacheLevel0SizeMatchesUnlocked(channel, frameCount))
            {
                buildState = gui::WaveformCache::makeFullBuildState(frameCount);
            }
            totalDirtyBlocks += gui::WaveformCache::dirtyBlockCount(buildState);
        }

        return totalDirtyBlocks;
    }

    void Document::clearWaveformCacheUnlocked()
    {
        for (auto &cache : waveformCache)
        {
            cache.clear();
        }
    }

    void Document::startWaveformCacheBuild()
    {
        WaveformCacheBuildJob *jobToStart = nullptr;
        {
            std::unique_lock lock(dataMutex);
            if (waveformCacheBuildJob || !needsWaveformCacheBuildUnlocked())
            {
                return;
            }
            waveformCacheAppliedProgress = {
                .completedBlocks = 0,
                .totalBlocks = totalWaveformCacheDirtyBlocksUnlocked(),
            };
            waveformCacheBuildJob = std::make_unique<WaveformCacheBuildJob>(this);
            jobToStart = waveformCacheBuildJob.get();
        }

        if (jobToStart)
        {
            jobToStart->start();
        }
    }

    void Document::stopWaveformCacheBuild()
    {
        std::unique_ptr<WaveformCacheBuildJob> oldJob;
        {
            std::unique_lock lock(dataMutex);
            oldJob = std::move(waveformCacheBuildJob);
            waveformCacheAppliedProgress.reset();
        }
    }

    std::optional<Document::WaveformCacheBuildRequest>
    Document::captureWaveformCacheBuildRequest() const
    {
        std::shared_lock lock(dataMutex);
        const auto frameCount = getFrameCountUnlocked();
        const auto channelCount = getChannelCountUnlocked();
        if (frameCount <= 0 || channelCount <= 0)
        {
            return std::nullopt;
        }

        WaveformCacheBuildRequest request;
        request.waveformDataVersion = waveformDataVersion;
        request.channels.reserve(static_cast<std::size_t>(channelCount));

        for (int64_t channel = 0; channel < channelCount; ++channel)
        {
            auto buildState =
                waveformCache[static_cast<std::size_t>(channel)].snapshotBuildState();
            if (!waveformCacheLevel0SizeMatchesUnlocked(channel, frameCount))
            {
                buildState = gui::WaveformCache::makeFullBuildState(frameCount);
            }

            const auto samples = buffer->getImmutableChannelData(channel);
            const auto totalDirtyBlocks =
                gui::WaveformCache::dirtyBlockCount(buildState);
            request.channels.push_back(
                {.channelIndex = channel,
                 .buildState = std::move(buildState),
                 .totalDirtyBlocks = totalDirtyBlocks});
        }

        return request;
    }

    void Document::syncWaveformCacheToChannelCount(const int64_t channelCount)
    {
        waveformCache.resize(static_cast<std::size_t>(channelCount));
    }

    void Document::resetWaveformCacheToChannelCount(const int64_t channelCount)
    {
        waveformCache.assign(static_cast<std::size_t>(channelCount),
                             gui::WaveformCache{});
    }

    int64_t Document::clampMarkerFrame(const int64_t frame) const
    {
        std::shared_lock lock(dataMutex);
        return clampMarkerFrameUnlocked(frame);
    }

    int64_t Document::getFrameCountUnlocked() const
    {
        return buffer->getFrameCount();
    }

    int64_t Document::getChannelCountUnlocked() const
    {
        return buffer->getChannelCount();
    }

    float Document::getSampleUnlocked(const int64_t channel,
                                      const int64_t frame) const
    {
        return buffer->getSample(channel, frame);
    }

    int64_t Document::clampMarkerFrameUnlocked(const int64_t frame) const
    {
        return std::clamp(frame, int64_t{0}, getFrameCountUnlocked());
    }

    void Document::normalizeMarkers()
    {
        std::unique_lock lock(dataMutex);
        normalizeMarkersUnlocked();
    }

    void Document::normalizeMarkersUnlocked()
    {
        uint64_t maxExistingId = 0;
        for (auto &marker : markers)
        {
            if (marker.id == 0)
            {
                marker.id = nextMarkerId++;
            }
            marker.frame = clampMarkerFrameUnlocked(marker.frame);
            maxExistingId = std::max(maxExistingId, marker.id);
        }

        nextMarkerId = std::max(nextMarkerId, maxExistingId + 1);
    }

    void Document::initialize(const SampleFormat sampleFormatToUse,
                              const uint32_t sampleRateToUse,
                              const uint32_t channelCount,
                              const int64_t frameCount)
    {
        std::unique_lock lock(dataMutex);
        format = sampleFormatToUse;
        sampleRate = sampleRateToUse;
        preservationSourceId = 0;
        const bool usesIntegerPcm =
            format == SampleFormat::PCM_S8 ||
            format == SampleFormat::PCM_S16 ||
            format == SampleFormat::PCM_S24 ||
            format == SampleFormat::PCM_S32;
        buffer = usesIntegerPcm
                     ? std::make_shared<
                           cupuacu::audio::PreservationTrackingAudioBuffer>()
                     : std::make_shared<cupuacu::audio::AudioBuffer>();
        buffer->resize(channelCount, frameCount);
        ++waveformDataVersion;
        ++markerDataVersion;
        resetWaveformCacheToChannelCount(channelCount);
        markers.clear();
        nextMarkerId = 1;
    }

    Document::ReadLease::ReadLease(const Document &documentToRead)
        : document(&documentToRead),
          lock(documentToRead.dataMutex)
    {
    }

    SampleFormat Document::ReadLease::getSampleFormat() const
    {
        return document->format;
    }

    int Document::ReadLease::getSampleRate() const
    {
        return document->sampleRate;
    }

    int64_t Document::ReadLease::getFrameCount() const
    {
        return document->getFrameCountUnlocked();
    }

    int64_t Document::ReadLease::getChannelCount() const
    {
        return document->getChannelCountUnlocked();
    }

    float Document::ReadLease::getSample(const int64_t channel,
                                         const int64_t frame) const
    {
        return document->getSampleUnlocked(channel, frame);
    }

    bool Document::ReadLease::isDirty(const int64_t channel,
                                      const int64_t frame) const
    {
        return document->buffer->isDirty(channel, frame);
    }

    cupuacu::audio::SampleProvenance
    Document::ReadLease::getSampleProvenance(const int64_t channel,
                                             const int64_t frame) const
    {
        return document->buffer->getProvenance(channel, frame);
    }

    uint64_t Document::ReadLease::getPreservationSourceId() const
    {
        return document->preservationSourceId;
    }

    const std::vector<DocumentMarker> &Document::ReadLease::getMarkers() const
    {
        return document->markers;
    }

    Document::ReadLease Document::acquireReadLease() const
    {
        return ReadLease(*this);
    }

    gui::WaveformCache &Document::getWaveformCache(const int channel)
    {
        return waveformCache[channel];
    }

    const gui::WaveformCache &Document::getWaveformCache(const int channel) const
    {
        return waveformCache[channel];
    }

    SampleFormat Document::getSampleFormat() const
    {
        std::shared_lock lock(dataMutex);
        return format;
    }

    int Document::getSampleRate() const
    {
        std::shared_lock lock(dataMutex);
        return sampleRate;
    }

    uint64_t Document::getWaveformDataVersion() const
    {
        std::shared_lock lock(dataMutex);
        return waveformDataVersion;
    }

    uint64_t Document::getMarkerDataVersion() const
    {
        std::shared_lock lock(dataMutex);
        return markerDataVersion;
    }

    int64_t Document::getFrameCount() const
    {
        std::shared_lock lock(dataMutex);
        return getFrameCountUnlocked();
    }

    int64_t Document::getChannelCount() const
    {
        std::shared_lock lock(dataMutex);
        return getChannelCountUnlocked();
    }

    float Document::getSample(int64_t channel, int64_t frame) const
    {
        std::shared_lock lock(dataMutex);
        return getSampleUnlocked(channel, frame);
    }

    void Document::setSample(int64_t channel, int64_t frame, float value,
                             const bool shouldMarkDirty)
    {
        std::unique_lock lock(dataMutex);
        buffer->setSample(channel, frame, value, shouldMarkDirty);
        ++waveformDataVersion;
    }

    void Document::writeInterleavedFloatBlock(const int64_t startFrame,
                                              const float *interleaved,
                                              const int64_t frameCount,
                                              const int64_t channelCount,
                                              const bool shouldMarkDirty)
    {
        std::unique_lock lock(dataMutex);
        if (!interleaved || startFrame < 0 || frameCount <= 0 ||
            channelCount <= 0)
        {
            return;
        }

        const auto writableFrames = std::min<int64_t>(
            frameCount,
            std::max<int64_t>(0, getFrameCountUnlocked() - startFrame));
        const auto writableChannels =
            std::min<int64_t>(channelCount, getChannelCountUnlocked());
        if (writableFrames <= 0 || writableChannels <= 0)
        {
            return;
        }

        if (shouldMarkDirty)
        {
            for (int64_t frame = 0; frame < writableFrames; ++frame)
            {
                for (int64_t channel = 0; channel < writableChannels; ++channel)
                {
                    buffer->setSample(
                        channel, startFrame + frame,
                        interleaved[static_cast<std::size_t>(frame) *
                                        static_cast<std::size_t>(
                                            channelCount) +
                                    static_cast<std::size_t>(channel)],
                        true);
                }
            }
            ++waveformDataVersion;
            return;
        }

        for (int64_t channel = 0; channel < writableChannels; ++channel)
        {
            auto channelData = buffer->getMutableChannelData(channel);
            if (channelData.empty())
            {
                continue;
            }
            for (int64_t frame = 0; frame < writableFrames; ++frame)
            {
                channelData[static_cast<std::size_t>(startFrame + frame)] =
                    interleaved[static_cast<std::size_t>(frame) *
                                    static_cast<std::size_t>(channelCount) +
                                static_cast<std::size_t>(channel)];
            }
        }
        ++waveformDataVersion;
    }

    void Document::resizeBuffer(int64_t channels, int64_t frames)
    {
        std::unique_lock lock(dataMutex);
        buffer->resize(channels, frames);
        syncWaveformCacheToChannelCount(channels);
        ++waveformDataVersion;
    }

    void Document::insertFrames(int64_t frameIndex, int64_t numFrames)
    {
        std::unique_lock lock(dataMutex);
        buffer->insertFrames(frameIndex, numFrames);
        ++waveformDataVersion;

        for (int ch = 0; ch < getChannelCountUnlocked(); ++ch)
        {
            waveformCache[ch].applyInsert(frameIndex, numFrames);
        }

        if (numFrames <= 0)
        {
            return;
        }

        for (auto &marker : markers)
        {
            if (marker.frame >= frameIndex)
            {
                marker.frame += numFrames;
            }
        }
        ++markerDataVersion;
    }

    void Document::removeFrames(
        int64_t frameIndex, int64_t numFrames,
        const SampleOperationProgressCallback &progress)
    {
        std::unique_lock lock(dataMutex);
        buffer->removeFrames(frameIndex, numFrames, progress);
        ++waveformDataVersion;

        for (int ch = 0; ch < getChannelCountUnlocked(); ++ch)
        {
            waveformCache[ch].applyErase(frameIndex, frameIndex + numFrames);
        }

        if (numFrames <= 0)
        {
            normalizeMarkersUnlocked();
            return;
        }

        const int64_t removedEnd = frameIndex + numFrames;
        for (auto &marker : markers)
        {
            if (marker.frame >= removedEnd)
            {
                marker.frame -= numFrames;
                continue;
            }

            if (marker.frame >= frameIndex)
            {
                marker.frame = frameIndex;
            }
        }

        normalizeMarkersUnlocked();
        ++markerDataVersion;
    }

    void Document::invalidateWaveformSamples(int64_t startSample,
                                             int64_t endSample)
    {
        std::unique_lock lock(dataMutex);
        for (int ch = 0; ch < getChannelCountUnlocked(); ++ch)
        {
            waveformCache[ch].invalidateSamples(startSample, endSample);
        }
    }

    void Document::updateWaveformCache()
    {
        (void)pumpWaveformCacheWork();
    }

    bool Document::pumpWaveformCacheWork()
    {
        // Each successful pump eventually invalidates and redraws the visible
        // waveform texture, so very small apply batches fragment UI-side work
        // too aggressively on large files.
        constexpr std::size_t kMaxPublishedOutputsPerPump = 256;

        bool applied = false;
        bool stateChanged = false;
        std::vector<WaveformCacheBuildOutput> publishedOutputs;
        bool buildCompleted = false;
        bool publishedOutputsRemain = false;
        {
            std::unique_lock lock(dataMutex);
            if (waveformCacheBuildJob)
            {
                publishedOutputs =
                    waveformCacheBuildJob->takePublishedOutputs(
                        kMaxPublishedOutputsPerPump);
                buildCompleted = waveformCacheBuildJob->isCompleted();
                publishedOutputsRemain =
                    waveformCacheBuildJob->hasPublishedOutputs();
            }
        }
        stateChanged = !publishedOutputs.empty();

        for (auto &output : publishedOutputs)
        {
            if (output.completed)
            {
                stateChanged = true;
                std::unique_lock lock(dataMutex);
                if (waveformCacheBuildJob && waveformCacheBuildJob->isCompleted())
                {
                    publishedOutputsRemain =
                        waveformCacheBuildJob->hasPublishedOutputs();
                    if (!publishedOutputsRemain)
                    {
                        waveformCacheBuildJob.reset();
                        waveformCacheAppliedProgress.reset();
                    }
                }
                continue;
            }

            std::unique_lock lock(dataMutex);
            if (output.waveformDataVersion != waveformDataVersion)
            {
                continue;
            }

            for (const auto &channelChunk : output.channelChunks)
            {
                if (channelChunk.channelIndex < 0 ||
                    channelChunk.channelIndex >=
                        static_cast<int64_t>(waveformCache.size()))
                {
                    continue;
                }

                waveformCache[static_cast<std::size_t>(channelChunk.channelIndex)]
                    .applyLevelSpanUpdates(
                        getFrameCountUnlocked(), channelChunk.builtFromBlock,
                        channelChunk.builtToBlock, channelChunk.levelUpdates);
                applied = true;
            }

            if (waveformCacheAppliedProgress.has_value())
            {
                waveformCacheAppliedProgress = {
                    .completedBlocks = std::max<int64_t>(
                        waveformCacheAppliedProgress->completedBlocks,
                        output.completedBlocks),
                    .totalBlocks = std::max<int64_t>(
                        waveformCacheAppliedProgress->totalBlocks,
                        output.totalBlocks),
                };
            }
            else
            {
                waveformCacheAppliedProgress = {
                    .completedBlocks = output.completedBlocks,
                    .totalBlocks = output.totalBlocks,
                };
            }
        }

        if (buildCompleted && !publishedOutputsRemain)
        {
            stateChanged = true;
            std::unique_lock lock(dataMutex);
            if (waveformCacheBuildJob && waveformCacheBuildJob->isCompleted())
            {
                waveformCacheBuildJob.reset();
                waveformCacheAppliedProgress.reset();
            }
        }

        {
            std::unique_lock lock(dataMutex);
            if (getFrameCountUnlocked() <= 0 || getChannelCountUnlocked() <= 0)
            {
                clearWaveformCacheUnlocked();
                return applied;
            }
        }

        startWaveformCacheBuild();
        return applied || stateChanged;
    }

    std::optional<Document::WaveformCacheBuildProgress>
    Document::getWaveformCacheBuildProgress() const
    {
        std::shared_lock lock(dataMutex);
        if (!waveformCacheBuildJob)
        {
            return std::nullopt;
        }
        auto progress = waveformCacheAppliedProgress.value_or(
            waveformCacheBuildJob->getProgress());
        if (progress.totalBlocks <= 0)
        {
            progress.completedBlocks = 0;
            progress.totalBlocks = totalWaveformCacheDirtyBlocksUnlocked();
        }
        return progress;
    }

    void Document::rebuildWaveformCacheSynchronously()
    {
        stopWaveformCacheBuild();

        std::unique_lock lock(dataMutex);
        const auto frameCount = getFrameCountUnlocked();
        const auto channelCount = getChannelCountUnlocked();
        if (frameCount <= 0 || channelCount <= 0)
        {
            clearWaveformCacheUnlocked();
            return;
        }

        for (int64_t channel = 0; channel < channelCount; ++channel)
        {
            const auto channelData = buffer->getImmutableChannelData(channel);
            auto buildState =
                waveformCache[static_cast<std::size_t>(channel)].snapshotBuildState();
            if (!waveformCacheLevel0SizeMatchesUnlocked(channel, frameCount))
            {
                buildState = gui::WaveformCache::makeFullBuildState(frameCount);
            }
            auto result = gui::WaveformCache::buildFromState(buildState,
                                                             channelData.data());
            waveformCache[static_cast<std::size_t>(channel)].applyBuildResult(
                std::move(result));
        }
    }

    std::shared_ptr<cupuacu::audio::AudioBuffer> Document::getAudioBuffer() const
    {
        std::shared_lock lock(dataMutex);
        return buffer;
    }

    uint64_t Document::getPreservationSourceId() const
    {
        std::shared_lock lock(dataMutex);
        return preservationSourceId;
    }

    cupuacu::audio::SampleProvenance
    Document::getSampleProvenance(const int64_t channel, const int64_t frame) const
    {
        std::shared_lock lock(dataMutex);
        return buffer->getProvenance(channel, frame);
    }

    void Document::setSampleProvenance(
        const int64_t channel, const int64_t frame,
        const cupuacu::audio::SampleProvenance &provenanceToUse)
    {
        std::unique_lock lock(dataMutex);
        buffer->setProvenance(channel, frame, provenanceToUse);
    }

    Document::AudioSegment Document::captureSegment(
        const int64_t startFrame, const int64_t frameCount,
        const SampleOperationProgressCallback &progress) const
    {
        std::shared_lock lock(dataMutex);
        AudioSegment result{};
        result.format = format;
        result.sampleRate = sampleRate;
        result.channelCount = getChannelCountUnlocked();
        result.frameCount = std::max<int64_t>(0, frameCount);
        result.samples.assign(static_cast<std::size_t>(result.channelCount), {});
        result.provenance.assign(static_cast<std::size_t>(result.channelCount), {});

        const auto boundedStart =
            std::clamp<int64_t>(startFrame, 0, getFrameCountUnlocked());
        const auto boundedCount = std::clamp<int64_t>(
            result.frameCount, 0, getFrameCountUnlocked() - boundedStart);
        result.frameCount = boundedCount;

        constexpr int64_t kProgressStrideFrames = 16384;
        const int64_t totalProgressUnits =
            boundedCount * std::max<int64_t>(1, result.channelCount);
        for (int64_t channel = 0; channel < result.channelCount; ++channel)
        {
            auto &channelSamples = result.samples[static_cast<std::size_t>(channel)];
            auto &channelProvenance =
                result.provenance[static_cast<std::size_t>(channel)];
            channelSamples.resize(static_cast<std::size_t>(boundedCount));
            channelProvenance.resize(static_cast<std::size_t>(boundedCount));
            for (int64_t frame = 0; frame < boundedCount; ++frame)
            {
                channelSamples[static_cast<std::size_t>(frame)] =
                    getSampleUnlocked(channel, boundedStart + frame);
                channelProvenance[static_cast<std::size_t>(frame)] =
                    buffer->getProvenance(channel, boundedStart + frame);
                if (progress &&
                    (((frame + 1) % kProgressStrideFrames) == 0 ||
                     frame + 1 == boundedCount))
                {
                    progress(channel * boundedCount + frame + 1,
                             totalProgressUnits);
                }
            }
        }

        return result;
    }

    void Document::assignSegment(
        const AudioSegment &segment,
        const SampleOperationProgressCallback &progress)
    {
        initialize(segment.format, static_cast<uint32_t>(segment.sampleRate),
                   static_cast<uint32_t>(segment.channelCount), segment.frameCount);
        writeSegment(0, segment, false, progress);
    }

    void Document::writeSegment(
        const int64_t startFrame, const AudioSegment &segment,
        const bool shouldMarkDirty,
        const SampleOperationProgressCallback &progress)
    {
        std::unique_lock lock(dataMutex);
        const auto writableFrames = std::min<int64_t>(
            segment.frameCount,
            std::max<int64_t>(0, getFrameCountUnlocked() - startFrame));
        const auto writableChannels =
            std::min<int64_t>(segment.channelCount, getChannelCountUnlocked());
        constexpr int64_t kProgressStrideFrames = 16384;
        const int64_t totalProgressUnits =
            writableFrames * std::max<int64_t>(1, writableChannels);

        for (int64_t channel = 0; channel < writableChannels; ++channel)
        {
            for (int64_t frame = 0; frame < writableFrames; ++frame)
            {
                buffer->setSample(
                    channel, startFrame + frame,
                    segment.samples[static_cast<std::size_t>(channel)]
                                   [static_cast<std::size_t>(frame)],
                    shouldMarkDirty);
                if (channel < static_cast<int64_t>(segment.provenance.size()) &&
                    frame < static_cast<int64_t>(
                                segment.provenance[static_cast<std::size_t>(channel)]
                                    .size()))
                {
                    buffer->setProvenance(
                        channel, startFrame + frame,
                        segment.provenance[static_cast<std::size_t>(channel)]
                                          [static_cast<std::size_t>(frame)]);
                }
                if (progress &&
                    (((frame + 1) % kProgressStrideFrames) == 0 ||
                     frame + 1 == writableFrames))
                {
                    progress(channel * writableFrames + frame + 1,
                             totalProgressUnits);
                }
            }
        }
        ++waveformDataVersion;
    }

    const std::vector<DocumentMarker> &Document::getMarkers() const
    {
        // Existing UI code treats this as a short-lived main-thread reference.
        // Background workers must use ReadLease instead.
        return markers;
    }

    uint64_t Document::addMarker(const int64_t frame, std::string label)
    {
        std::unique_lock lock(dataMutex);
        const uint64_t id = nextMarkerId++;
        markers.push_back(DocumentMarker{
            .id = id,
            .frame = clampMarkerFrameUnlocked(frame),
            .label = std::move(label),
        });
        ++markerDataVersion;
        return id;
    }

    bool Document::removeMarker(const uint64_t id)
    {
        std::unique_lock lock(dataMutex);
        const auto beforeSize = markers.size();
        markers.erase(std::remove_if(markers.begin(), markers.end(),
                                     [&](const DocumentMarker &marker)
                                     { return marker.id == id; }),
                      markers.end());
        const bool changed = markers.size() != beforeSize;
        if (changed)
        {
            ++markerDataVersion;
        }
        return changed;
    }

    bool Document::setMarkerFrame(const uint64_t id, const int64_t frame)
    {
        std::unique_lock lock(dataMutex);
        for (auto &marker : markers)
        {
            if (marker.id == id)
            {
                marker.frame = clampMarkerFrameUnlocked(frame);
                ++markerDataVersion;
                return true;
            }
        }
        return false;
    }

    bool Document::setMarkerLabel(const uint64_t id, std::string label)
    {
        std::unique_lock lock(dataMutex);
        for (auto &marker : markers)
        {
            if (marker.id == id)
            {
                marker.label = std::move(label);
                ++markerDataVersion;
                return true;
            }
        }
        return false;
    }

    void Document::replaceMarkers(std::vector<DocumentMarker> markersToUse)
    {
        std::unique_lock lock(dataMutex);
        markers = std::move(markersToUse);
        normalizeMarkersUnlocked();
        ++markerDataVersion;
    }

    void Document::clearMarkers()
    {
        std::unique_lock lock(dataMutex);
        markers.clear();
        ++markerDataVersion;
    }

    void Document::markCurrentStateAsSavedSource()
    {
        std::unique_lock lock(dataMutex);
        preservationSourceId = nextPreservationSourceId();
        buffer->establishSequentialProvenance(preservationSourceId);
    }
} // namespace cupuacu
