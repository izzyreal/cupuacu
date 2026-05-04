#pragma once

#include "audio/AudioBuffer.hpp"
#include "audio/SampleProvenance.hpp"
#include "SampleFormat.hpp"

#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
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
        using SampleOperationProgressCallback =
            std::function<void(int64_t completed, int64_t total)>;

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
        std::shared_ptr<cupuacu::audio::AudioBuffer> buffer =
            std::make_shared<cupuacu::audio::AudioBuffer>();
        int sampleRate = 0;
        SampleFormat format = SampleFormat::Unknown;
        uint64_t preservationSourceId = 0;
        uint64_t waveformDataVersion = 0;
        uint64_t markerDataVersion = 0;
        uint64_t nextMarkerId = 1;
        mutable std::shared_mutex dataMutex;
        std::vector<DocumentMarker> markers;

        int64_t getFrameCountUnlocked() const;
        int64_t getChannelCountUnlocked() const;
        float getSampleUnlocked(int64_t channel, int64_t frame) const;
        int64_t clampMarkerFrame(int64_t frame) const;
        int64_t clampMarkerFrameUnlocked(int64_t frame) const;
        void normalizeMarkers();
        void normalizeMarkersUnlocked();

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

        void insertFrames(
            int64_t frameIndex, int64_t numFrames,
            const SampleOperationProgressCallback &progress = {});

        void removeFrames(
            int64_t frameIndex, int64_t numFrames,
            const SampleOperationProgressCallback &progress = {});

        std::shared_ptr<cupuacu::audio::AudioBuffer> getAudioBuffer() const;
        uint64_t getPreservationSourceId() const;
        audio::SampleProvenance getSampleProvenance(int64_t channel,
                                                    int64_t frame) const;
        void setSampleProvenance(int64_t channel, int64_t frame,
                                 const audio::SampleProvenance &provenance);
        AudioSegment captureSegment(
            int64_t startFrame, int64_t frameCount,
            const SampleOperationProgressCallback &progress = {}) const;
        void assignSegment(
            const AudioSegment &segment,
            const SampleOperationProgressCallback &progress = {});
        void writeSegment(int64_t startFrame, const AudioSegment &segment,
                          bool shouldMarkDirty = false,
                          const SampleOperationProgressCallback &progress = {});
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
