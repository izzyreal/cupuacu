#pragma once

#include "audio/AudioBuffer.hpp"
#include "audio/SampleProvenance.hpp"
#include "SampleFormat.hpp"
#include "gui/WaveformCache.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

namespace cupuacu
{
    struct DocumentMarker
    {
        uint64_t id = 0;
        int64_t frame = 0;
        std::string label;

        bool operator==(const DocumentMarker &) const = default;
    };

    class Document
    {
    public:
        struct AudioSegment
        {
            SampleFormat format = SampleFormat::Unknown;
            int sampleRate = 0;
            int64_t channelCount = 0;
            int64_t frameCount = 0;
            std::vector<std::vector<float>> samples;
            std::vector<std::vector<audio::SampleProvenance>> provenance;
        };

    private:
        struct WaveformCacheBuildRequestChannel
        {
            int64_t channelIndex = 0;
            gui::WaveformCache::BuildState buildState;
            int64_t totalDirtyBlocks = 0;
        };

        struct WaveformCacheBuildRequest
        {
            uint64_t waveformDataVersion = 0;
            std::vector<WaveformCacheBuildRequestChannel> channels;
        };

        struct WaveformCacheBuildOutput
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

        struct WaveformCacheBuildProgress
        {
            int64_t completedBlocks = 0;
            int64_t totalBlocks = 0;
        };

        class WaveformCacheBuildJob
        {
        public:
            explicit WaveformCacheBuildJob(const Document *documentToRead);
            ~WaveformCacheBuildJob();

            WaveformCacheBuildJob(const WaveformCacheBuildJob &) = delete;
            WaveformCacheBuildJob &
            operator=(const WaveformCacheBuildJob &) = delete;

            void start();
            [[nodiscard]] bool isCompleted() const;
            [[nodiscard]] std::vector<WaveformCacheBuildOutput> takePublishedOutputs();
            [[nodiscard]] WaveformCacheBuildProgress getProgress() const;

        private:
            const Document *document = nullptr;
            mutable std::mutex mutex;
            bool completed = false;
            WaveformCacheBuildProgress progress;
            std::vector<WaveformCacheBuildOutput> outputs;
            std::thread worker;

            void run();
        };

        std::shared_ptr<cupuacu::audio::AudioBuffer> buffer =
            std::make_shared<cupuacu::audio::AudioBuffer>();
        int sampleRate = 0;
        SampleFormat format = SampleFormat::Unknown;
        uint64_t preservationSourceId = 0;
        uint64_t waveformDataVersion = 0;
        uint64_t markerDataVersion = 0;
        uint64_t nextMarkerId = 1;
        mutable std::shared_mutex dataMutex;
        std::vector<gui::WaveformCache> waveformCache =
            std::vector<gui::WaveformCache>(2);
        std::vector<DocumentMarker> markers;
        std::unique_ptr<WaveformCacheBuildJob> waveformCacheBuildJob;

        void syncWaveformCacheToChannelCount(int64_t channelCount);
        void resetWaveformCacheToChannelCount(int64_t channelCount);
        int64_t getFrameCountUnlocked() const;
        int64_t getChannelCountUnlocked() const;
        float getSampleUnlocked(int64_t channel, int64_t frame) const;
        int64_t clampMarkerFrame(int64_t frame) const;
        int64_t clampMarkerFrameUnlocked(int64_t frame) const;
        void normalizeMarkers();
        void normalizeMarkersUnlocked();
        [[nodiscard]] bool needsWaveformCacheBuildUnlocked() const;
        [[nodiscard]] int64_t totalWaveformCacheDirtyBlocksUnlocked() const;
        [[nodiscard]] bool
        waveformCacheLevel0SizeMatchesUnlocked(int64_t channel,
                                               int64_t frameCount) const;
        void clearWaveformCacheUnlocked();
        void startWaveformCacheBuild();
        void stopWaveformCacheBuild();
        [[nodiscard]] std::optional<WaveformCacheBuildRequest>
        captureWaveformCacheBuildRequest() const;

    public:
        Document() = default;
        ~Document();
        Document(const Document &other);
        Document &operator=(const Document &other);
        Document(Document &&other) noexcept;
        Document &operator=(Document &&other) noexcept;

        class ReadLease
        {
        public:
            ReadLease(const ReadLease &) = delete;
            ReadLease &operator=(const ReadLease &) = delete;
            ReadLease(ReadLease &&) = default;
            ReadLease &operator=(ReadLease &&) = default;

            [[nodiscard]] SampleFormat getSampleFormat() const;
            [[nodiscard]] int getSampleRate() const;
            [[nodiscard]] int64_t getFrameCount() const;
            [[nodiscard]] int64_t getChannelCount() const;
            [[nodiscard]] float getSample(int64_t channel, int64_t frame) const;
            [[nodiscard]] bool isDirty(int64_t channel, int64_t frame) const;
            [[nodiscard]] audio::SampleProvenance
            getSampleProvenance(int64_t channel, int64_t frame) const;
            [[nodiscard]] uint64_t getPreservationSourceId() const;
            [[nodiscard]] const std::vector<DocumentMarker> &getMarkers() const;

        private:
            friend class Document;

            explicit ReadLease(const Document &documentToRead);

            const Document *document = nullptr;
            std::shared_lock<std::shared_mutex> lock;
        };

        void initialize(SampleFormat sampleFormatToUse,
                        uint32_t sampleRateToUse,
                        uint32_t channelCount, int64_t frameCount);

        [[nodiscard]] ReadLease acquireReadLease() const;

        gui::WaveformCache &getWaveformCache(int channel);
        const gui::WaveformCache &getWaveformCache(int channel) const;

        SampleFormat getSampleFormat() const;

        int getSampleRate() const;
        uint64_t getWaveformDataVersion() const;
        uint64_t getMarkerDataVersion() const;
        int64_t getFrameCount() const;
        int64_t getChannelCount() const;

        float getSample(int64_t channel, int64_t frame) const;
        void setSample(int64_t channel, int64_t frame, float value,
                       bool shouldMarkDirty = true);
        void writeInterleavedFloatBlock(int64_t startFrame,
                                        const float *interleaved,
                                        int64_t frameCount,
                                        int64_t channelCount,
                                        bool shouldMarkDirty = false);

        void resizeBuffer(int64_t channels, int64_t frames);

        void insertFrames(int64_t frameIndex, int64_t numFrames);

        void removeFrames(int64_t frameIndex, int64_t numFrames);
        void invalidateWaveformSamples(int64_t startSample, int64_t endSample);

        void updateWaveformCache();
        bool pumpWaveformCacheWork();
        [[nodiscard]] std::optional<WaveformCacheBuildProgress>
        getWaveformCacheBuildProgress() const;
        void rebuildWaveformCacheSynchronously();

        std::shared_ptr<cupuacu::audio::AudioBuffer> getAudioBuffer() const;
        uint64_t getPreservationSourceId() const;
        audio::SampleProvenance getSampleProvenance(int64_t channel,
                                                    int64_t frame) const;
        void setSampleProvenance(int64_t channel, int64_t frame,
                                 const audio::SampleProvenance &provenance);
        AudioSegment captureSegment(int64_t startFrame, int64_t frameCount) const;
        void assignSegment(const AudioSegment &segment);
        void writeSegment(int64_t startFrame, const AudioSegment &segment,
                          bool shouldMarkDirty = false);
        const std::vector<DocumentMarker> &getMarkers() const;
        uint64_t addMarker(int64_t frame, std::string label = {});
        bool removeMarker(uint64_t id);
        bool setMarkerFrame(uint64_t id, int64_t frame);
        bool setMarkerLabel(uint64_t id, std::string label);
        void replaceMarkers(std::vector<DocumentMarker> markersToUse);
        void clearMarkers();
        void markCurrentStateAsSavedSource();
    };
} // namespace cupuacu
