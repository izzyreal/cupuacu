#pragma once

#include "../gui/WaveformCache.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace cupuacu
{
    class Document;
}

namespace cupuacu::waveform
{
    class DocumentWaveformCaches
    {
    public:
        struct BuildProgress
        {
            int64_t completedBlocks = 0;
            int64_t totalBlocks = 0;
        };

        DocumentWaveformCaches() = default;
        ~DocumentWaveformCaches();

        DocumentWaveformCaches(const DocumentWaveformCaches &other);
        DocumentWaveformCaches &
        operator=(const DocumentWaveformCaches &other);
        DocumentWaveformCaches(DocumentWaveformCaches &&other) noexcept = default;
        DocumentWaveformCaches &
        operator=(DocumentWaveformCaches &&other) noexcept = default;

        void stopBuild();
        void syncToChannelCount(int64_t channelCount);
        void resetToChannelCount(int64_t channelCount);

        gui::WaveformCache &getCache(int channel);
        const gui::WaveformCache &getCache(int channel) const;

        void applyInsert(int64_t frameIndex, int64_t numFrames);
        void applyErase(int64_t frameIndex, int64_t numFrames);
        void invalidateSamples(int64_t startSample, int64_t endSample);

        void update(const Document &document, uint64_t waveformDataVersion);
        bool pumpWork(const Document &document, uint64_t waveformDataVersion);
        [[nodiscard]] std::optional<BuildProgress>
        getBuildProgress(const Document &document,
                         uint64_t waveformDataVersion) const;
        void rebuildSynchronously(const Document &document);

    private:
        struct BuildRequestChannel
        {
            int64_t channelIndex = 0;
            gui::WaveformCache::BuildState buildState;
            int64_t totalDirtyBlocks = 0;
        };

        struct BuildRequest
        {
            uint64_t waveformDataVersion = 0;
            std::vector<BuildRequestChannel> channels;
        };

        struct BuildOutput
        {
            struct ChannelChunk
            {
                int64_t channelIndex = 0;
                int64_t builtFromBlock = 0;
                int64_t builtToBlock = -1;
                std::vector<gui::WaveformCache::LevelSpanUpdate> levelUpdates;
            };

            uint64_t waveformDataVersion = 0;
            int64_t completedBlocks = 0;
            int64_t totalBlocks = 0;
            bool completed = false;
            std::vector<ChannelChunk> channelChunks;
        };

        class BuildJob
        {
        public:
            BuildJob(const Document *documentToRead, BuildRequest requestToUse);
            ~BuildJob();

            BuildJob(const BuildJob &) = delete;
            BuildJob &operator=(const BuildJob &) = delete;

            void start();
            [[nodiscard]] bool isCompleted() const;
            [[nodiscard]] std::vector<BuildOutput>
            takePublishedOutputs(std::size_t maxCount);
            [[nodiscard]] bool hasPublishedOutputs() const;
            [[nodiscard]] BuildProgress getProgress() const;

        private:
            const Document *document = nullptr;
            BuildRequest request;
            mutable std::mutex mutex;
            bool completed = false;
            BuildProgress progress;
            std::deque<BuildOutput> outputs;
            std::thread worker;

            void run();
        };

        std::vector<gui::WaveformCache> caches = std::vector<gui::WaveformCache>(2);
        std::unique_ptr<BuildJob> buildJob;
        std::optional<BuildProgress> appliedProgress;

        [[nodiscard]] bool level0SizeMatches(int64_t channel,
                                             int64_t frameCount) const;
        [[nodiscard]] bool needsBuild(int64_t frameCount,
                                      int64_t channelCount) const;
        [[nodiscard]] int64_t totalDirtyBlocks(int64_t frameCount,
                                               int64_t channelCount) const;
        void clear();
        [[nodiscard]] std::optional<BuildRequest>
        makeBuildRequest(int64_t frameCount, int64_t channelCount,
                         uint64_t waveformDataVersion) const;
        void startBuild(const Document &document, int64_t frameCount,
                        int64_t channelCount, uint64_t waveformDataVersion);
    };
} // namespace cupuacu::waveform
