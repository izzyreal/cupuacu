#pragma once

#include "audio/AudioBuffer.hpp"
#include "audio/DirtyTrackingAudioBuffer.hpp"
#include "SampleFormat.hpp"
#include "gui/WaveformCache.hpp"

#include <memory>
#include <vector>

namespace cupuacu
{
    class Document
    {
    private:
        std::shared_ptr<cupuacu::audio::AudioBuffer> buffer =
            std::make_shared<cupuacu::audio::AudioBuffer>();
        int sampleRate = 0;
        SampleFormat format = SampleFormat::Unknown;
        uint64_t waveformDataVersion = 0;
        std::vector<gui::WaveformCache> waveformCache =
            std::vector<gui::WaveformCache>(2);

        void syncWaveformCacheToChannelCount(int64_t channelCount);
        void resetWaveformCacheToChannelCount(int64_t channelCount);

    public:
        void initialize(SampleFormat sampleFormatToUse,
                        uint32_t sampleRateToUse,
                        uint32_t channelCount, int64_t frameCount);

        gui::WaveformCache &getWaveformCache(int channel);
        const gui::WaveformCache &getWaveformCache(int channel) const;

        SampleFormat getSampleFormat() const;

        int getSampleRate() const;
        uint64_t getWaveformDataVersion() const;
        int64_t getFrameCount() const;
        int64_t getChannelCount() const;

        float getSample(int64_t channel, int64_t frame) const;
        void setSample(int64_t channel, int64_t frame, float value,
                       bool shouldMarkDirty = true);

        void resizeBuffer(int64_t channels, int64_t frames);

        void insertFrames(int64_t frameIndex, int64_t numFrames);

        void removeFrames(int64_t frameIndex, int64_t numFrames);
        void invalidateWaveformSamples(int64_t startSample, int64_t endSample);

        void updateWaveformCache();

        std::shared_ptr<cupuacu::audio::AudioBuffer> getAudioBuffer() const;
    };
} // namespace cupuacu
