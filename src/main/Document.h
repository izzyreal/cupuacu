#pragma once

#include "AudioBuffer.h"
#include "DirtyTrackingAudioBuffer.h"
#include "SampleFormat.h"
#include "gui/WaveformCache.h"

#include <memory>
#include <vector>

namespace cupuacu {
    class Document {
        private:
            std::shared_ptr<cupuacu::AudioBuffer> buffer = std::make_shared<cupuacu::AudioBuffer>();
            int sampleRate = 0;
            SampleFormat format = SampleFormat::Unknown;
            std::vector<gui::WaveformCache> waveformCache = std::vector<gui::WaveformCache>(2);

        public:
            void initialize(const SampleFormat sampleFormatToUse, const uint32_t sampleRateToUse, const uint32_t channelCount, const int64_t frameCount)
            {
                format = sampleFormatToUse;
                sampleRate = sampleRateToUse;
                buffer = format == SampleFormat::PCM_S16 ? std::make_shared<cupuacu::DirtyTrackingAudioBuffer>() : std::make_shared<cupuacu::AudioBuffer>();
                buffer->resize(channelCount, frameCount);
            }

            gui::WaveformCache &getWaveformCache(const int channel)
            {
                return waveformCache[channel];
            }

            SampleFormat getSampleFormat() { return format; }

            int getSampleRate() const { return sampleRate; }
            int64_t getFrameCount() const { return buffer->getFrameCount(); }
            int64_t getChannelCount() const { return buffer->getChannelCount(); }

            float getSample(int64_t channel, int64_t frame) const { return buffer->getSample(channel, frame); }
            void setSample(int64_t channel, int64_t frame, float value, const bool shouldMarkDirty = true) { buffer->setSample(channel, frame, value, shouldMarkDirty); }

            void resizeBuffer(int64_t channels, int64_t frames) { buffer->resize(channels, frames); }
            
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
                    waveformCache[ch].applyErase(frameIndex, frameIndex + numFrames);
                }
            }

            void updateWaveformCache()
            {
                for (int ch = 0; ch < getChannelCount(); ++ch)
                {
                    waveformCache[ch].rebuildDirty(getAudioBuffer()->getImmutableChannelData(ch).data());
                }
            }

            const std::shared_ptr<cupuacu::AudioBuffer> getAudioBuffer() const { return buffer; }
    };
}
