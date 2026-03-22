#include "Document.hpp"

namespace cupuacu
{
    void Document::syncWaveformCacheToChannelCount(const int64_t channelCount)
    {
        waveformCache.resize(static_cast<std::size_t>(channelCount));
    }

    void Document::resetWaveformCacheToChannelCount(const int64_t channelCount)
    {
        waveformCache.assign(static_cast<std::size_t>(channelCount),
                             gui::WaveformCache{});
    }

    void Document::initialize(const SampleFormat sampleFormatToUse,
                              const uint32_t sampleRateToUse,
                              const uint32_t channelCount,
                              const int64_t frameCount)
    {
        format = sampleFormatToUse;
        sampleRate = sampleRateToUse;
        const bool usesIntegerPcm =
            format == SampleFormat::PCM_S8 ||
            format == SampleFormat::PCM_S16 ||
            format == SampleFormat::PCM_S24 ||
            format == SampleFormat::PCM_S32;
        buffer = usesIntegerPcm
                     ? std::make_shared<cupuacu::audio::DirtyTrackingAudioBuffer>()
                     : std::make_shared<cupuacu::audio::AudioBuffer>();
        buffer->resize(channelCount, frameCount);
        ++waveformDataVersion;
        resetWaveformCacheToChannelCount(channelCount);
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
    }

    void Document::removeFrames(int64_t frameIndex, int64_t numFrames)
    {
        buffer->removeFrames(frameIndex, numFrames);
        ++waveformDataVersion;

        for (int ch = 0; ch < getChannelCount(); ++ch)
        {
            waveformCache[ch].applyErase(frameIndex, frameIndex + numFrames);
        }
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
} // namespace cupuacu
