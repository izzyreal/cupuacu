#include "Document.hpp"

#include "audio/PreservationTrackingAudioBuffer.hpp"

#include <algorithm>

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
        return std::clamp(frame, int64_t{0}, getFrameCount());
    }

    void Document::normalizeMarkers()
    {
        uint64_t maxExistingId = 0;
        for (auto &marker : markers)
        {
            if (marker.id == 0)
            {
                marker.id = nextMarkerId++;
            }
            marker.frame = clampMarkerFrame(marker.frame);
            maxExistingId = std::max(maxExistingId, marker.id);
        }

        nextMarkerId = std::max(nextMarkerId, maxExistingId + 1);
    }

    void Document::initialize(const SampleFormat sampleFormatToUse,
                              const uint32_t sampleRateToUse,
                              const uint32_t channelCount,
                              const int64_t frameCount)
    {
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
        return format;
    }

    int Document::getSampleRate() const
    {
        return sampleRate;
    }

    uint64_t Document::getWaveformDataVersion() const
    {
        return waveformDataVersion;
    }

    uint64_t Document::getMarkerDataVersion() const
    {
        return markerDataVersion;
    }

    int64_t Document::getFrameCount() const
    {
        return buffer->getFrameCount();
    }

    int64_t Document::getChannelCount() const
    {
        return buffer->getChannelCount();
    }

    float Document::getSample(int64_t channel, int64_t frame) const
    {
        return buffer->getSample(channel, frame);
    }

    void Document::setSample(int64_t channel, int64_t frame, float value,
                             const bool shouldMarkDirty)
    {
        buffer->setSample(channel, frame, value, shouldMarkDirty);
        ++waveformDataVersion;
    }

    void Document::resizeBuffer(int64_t channels, int64_t frames)
    {
        buffer->resize(channels, frames);
        syncWaveformCacheToChannelCount(channels);
        ++waveformDataVersion;
    }

    void Document::insertFrames(int64_t frameIndex, int64_t numFrames)
    {
        buffer->insertFrames(frameIndex, numFrames);
        ++waveformDataVersion;

        for (int ch = 0; ch < getChannelCount(); ++ch)
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
        buffer->removeFrames(frameIndex, numFrames);
        ++waveformDataVersion;

        for (int ch = 0; ch < getChannelCount(); ++ch)
        {
            waveformCache[ch].applyErase(frameIndex, frameIndex + numFrames);
        }

        if (numFrames <= 0)
        {
            normalizeMarkers();
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

        normalizeMarkers();
        ++markerDataVersion;
    }

    void Document::invalidateWaveformSamples(int64_t startSample,
                                             int64_t endSample)
    {
        for (int ch = 0; ch < getChannelCount(); ++ch)
        {
            waveformCache[ch].invalidateSamples(startSample, endSample);
        }
    }

    void Document::updateWaveformCache()
    {
        for (int ch = 0; ch < getChannelCount(); ++ch)
        {
            const auto channelData = getAudioBuffer()->getImmutableChannelData(ch);
            const auto expectedLevel0Size =
                getFrameCount() <= 0
                    ? 0
                    : (getFrameCount() +
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
                waveformCache[ch].rebuildAll(channelData.data(), getFrameCount());
            }
            else
            {
                waveformCache[ch].rebuildDirty(channelData.data());
            }
        }
    }

    std::shared_ptr<cupuacu::audio::AudioBuffer> Document::getAudioBuffer() const
    {
        return buffer;
    }

    uint64_t Document::getPreservationSourceId() const
    {
        return preservationSourceId;
    }

    cupuacu::audio::SampleProvenance
    Document::getSampleProvenance(const int64_t channel, const int64_t frame) const
    {
        return buffer->getProvenance(channel, frame);
    }

    void Document::setSampleProvenance(
        const int64_t channel, const int64_t frame,
        const cupuacu::audio::SampleProvenance &provenanceToUse)
    {
        buffer->setProvenance(channel, frame, provenanceToUse);
    }

    Document::AudioSegment Document::captureSegment(const int64_t startFrame,
                                                    const int64_t frameCount) const
    {
        AudioSegment result{};
        result.format = format;
        result.sampleRate = sampleRate;
        result.channelCount = getChannelCount();
        result.frameCount = std::max<int64_t>(0, frameCount);
        result.samples.assign(static_cast<std::size_t>(result.channelCount), {});
        result.provenance.assign(static_cast<std::size_t>(result.channelCount), {});

        const auto boundedStart = std::clamp<int64_t>(startFrame, 0, getFrameCount());
        const auto boundedCount = std::clamp<int64_t>(
            result.frameCount, 0, getFrameCount() - boundedStart);
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
                    getSample(channel, boundedStart + frame);
                channelProvenance[static_cast<std::size_t>(frame)] =
                    getSampleProvenance(channel, boundedStart + frame);
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
        const auto writableFrames = std::min<int64_t>(
            segment.frameCount, std::max<int64_t>(0, getFrameCount() - startFrame));
        const auto writableChannels =
            std::min<int64_t>(segment.channelCount, getChannelCount());

        for (int64_t channel = 0; channel < writableChannels; ++channel)
        {
            for (int64_t frame = 0; frame < writableFrames; ++frame)
            {
                setSample(channel, startFrame + frame,
                          segment.samples[static_cast<std::size_t>(channel)]
                                         [static_cast<std::size_t>(frame)],
                          shouldMarkDirty);
                if (channel < static_cast<int64_t>(segment.provenance.size()) &&
                    frame < static_cast<int64_t>(
                                segment.provenance[static_cast<std::size_t>(channel)]
                                    .size()))
                {
                    setSampleProvenance(
                        channel, startFrame + frame,
                        segment.provenance[static_cast<std::size_t>(channel)]
                                          [static_cast<std::size_t>(frame)]);
                }
            }
        }
    }

    const std::vector<DocumentMarker> &Document::getMarkers() const
    {
        return markers;
    }

    uint64_t Document::addMarker(const int64_t frame, std::string label)
    {
        const uint64_t id = nextMarkerId++;
        markers.push_back(DocumentMarker{
            .id = id,
            .frame = clampMarkerFrame(frame),
            .label = std::move(label),
        });
        ++markerDataVersion;
        return id;
    }

    bool Document::removeMarker(const uint64_t id)
    {
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
        for (auto &marker : markers)
        {
            if (marker.id == id)
            {
                marker.frame = clampMarkerFrame(frame);
                ++markerDataVersion;
                return true;
            }
        }
        return false;
    }

    bool Document::setMarkerLabel(const uint64_t id, std::string label)
    {
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
        markers = std::move(markersToUse);
        normalizeMarkers();
        ++markerDataVersion;
    }

    void Document::clearMarkers()
    {
        markers.clear();
        ++markerDataVersion;
    }

    void Document::markCurrentStateAsSavedSource()
    {
        preservationSourceId = nextPreservationSourceId();
        buffer->establishSequentialProvenance(preservationSourceId);
    }
} // namespace cupuacu
