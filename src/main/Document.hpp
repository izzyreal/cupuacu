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
        std::vector<gui::WaveformCache> waveformCache =
            std::vector<gui::WaveformCache>(2);

    public:
        void initialize(const SampleFormat sampleFormatToUse,
                        const uint32_t sampleRateToUse,
                        const uint32_t channelCount, const int64_t frameCount)
        {
            format = sampleFormatToUse;
            sampleRate = sampleRateToUse;
            buffer = format == SampleFormat::PCM_S16
                         ? std::make_shared<
                               cupuacu::audio::DirtyTrackingAudioBuffer>()
                         : std::make_shared<cupuacu::audio::AudioBuffer>();
            buffer->resize(channelCount, frameCount);
            waveformCache.assign(channelCount, gui::WaveformCache{});
        }

        gui::WaveformCache &getWaveformCache(const int channel)
        {
            return waveformCache[channel];
        }
        const gui::WaveformCache &getWaveformCache(const int channel) const
        {
            return waveformCache[channel];
        }

        SampleFormat getSampleFormat()
        {
            return format;
        }

        int getSampleRate() const
        {
            return sampleRate;
        }
        int64_t getFrameCount() const
        {
            return buffer->getFrameCount();
        }
        int64_t getChannelCount() const
        {
            return buffer->getChannelCount();
        }

        float getSample(int64_t channel, int64_t frame) const
        {
            return buffer->getSample(channel, frame);
        }
        void setSample(int64_t channel, int64_t frame, float value,
                       const bool shouldMarkDirty = true)
        {
            buffer->setSample(channel, frame, value, shouldMarkDirty);
        }

        void resizeBuffer(int64_t channels, int64_t frames)
        {
            buffer->resize(channels, frames);
        }

        void insertFrames(int64_t frameIndex, int64_t numFrames)
        {
            buffer->insertFrames(frameIndex, numFrames);

            for (int ch = 0; ch < getChannelCount(); ++ch)
            {
                waveformCache[ch].applyInsert(frameIndex, numFrames);
            }
        }

        void removeFrames(int64_t frameIndex, int64_t numFrames)
        {
            buffer->removeFrames(frameIndex, numFrames);

            for (int ch = 0; ch < getChannelCount(); ++ch)
            {
                waveformCache[ch].applyErase(frameIndex,
                                             frameIndex + numFrames);
            }
        }

        void updateWaveformCache()
        {
            for (int ch = 0; ch < getChannelCount(); ++ch)
            {
                const auto channelData =
                    getAudioBuffer()->getImmutableChannelData(ch);
                if (waveformCache[ch].levelsCount() == 0)
                {
                    waveformCache[ch].rebuildAll(channelData.data(),
                                                 getFrameCount());
                }
                else
                {
                    waveformCache[ch].rebuildDirty(channelData.data());
                }
            }
        }

        const std::shared_ptr<cupuacu::audio::AudioBuffer>
        getAudioBuffer() const
        {
            return buffer;
        }
    };
} // namespace cupuacu
