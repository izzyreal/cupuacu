#include "Document.hpp"

#include "audio/PreservationTrackingAudioBuffer.hpp"

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <shared_mutex>

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

    void Document::removeFrames(int64_t frameIndex, int64_t numFrames)
    {
        std::unique_lock lock(dataMutex);
        buffer->removeFrames(frameIndex, numFrames);
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
        std::unique_lock lock(dataMutex);
        for (int ch = 0; ch < getChannelCountUnlocked(); ++ch)
        {
            const auto channelData = buffer->getImmutableChannelData(ch);
            const auto expectedLevel0Size =
                getFrameCountUnlocked() <= 0
                    ? 0
                    : (getFrameCountUnlocked() +
                       gui::WaveformCache::BASE_BLOCK_SIZE - 1) /
                          gui::WaveformCache::BASE_BLOCK_SIZE;
            const bool hasLevels = waveformCache[ch].levelsCount() > 0;
            const bool level0SizeMatches =
                hasLevels &&
                static_cast<int64_t>(
                    waveformCache[ch].getLevelByIndex(0).size()) ==
                    expectedLevel0Size;

            if (!hasLevels || !level0SizeMatches)
            {
                waveformCache[ch].rebuildAll(channelData.data(),
                                             getFrameCountUnlocked());
            }
            else
            {
                waveformCache[ch].rebuildDirty(channelData.data());
            }
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

    Document::AudioSegment Document::captureSegment(const int64_t startFrame,
                                                    const int64_t frameCount) const
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
            }
        }

        return result;
    }

    void Document::assignSegment(const AudioSegment &segment)
    {
        initialize(segment.format, static_cast<uint32_t>(segment.sampleRate),
                   static_cast<uint32_t>(segment.channelCount), segment.frameCount);
        writeSegment(0, segment, false);
    }

    void Document::writeSegment(const int64_t startFrame, const AudioSegment &segment,
                                const bool shouldMarkDirty)
    {
        std::unique_lock lock(dataMutex);
        const auto writableFrames = std::min<int64_t>(
            segment.frameCount,
            std::max<int64_t>(0, getFrameCountUnlocked() - startFrame));
        const auto writableChannels =
            std::min<int64_t>(segment.channelCount, getChannelCountUnlocked());

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
