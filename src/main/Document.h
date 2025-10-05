#pragma once

#include "AudioBuffer.h"
#include "DirtyTrackingAudioBuffer.h"
#include "SampleFormat.h"

#include <memory>

namespace cupuacu {
class Document {
private:
    std::shared_ptr<cupuacu::AudioBuffer> buffer = std::make_shared<cupuacu::AudioBuffer>();
    int sampleRate = 0;
    SampleFormat format = SampleFormat::Unknown;

public:
    void initialize(const SampleFormat sampleFormatToUse, const uint32_t sampleRateToUse, const uint32_t channelCount, const int64_t frameCount)
    {
        format = sampleFormatToUse;
        sampleRate = sampleRateToUse;
        buffer = format == SampleFormat::PCM_S16 ? std::make_shared<cupuacu::DirtyTrackingAudioBuffer>() : std::make_shared<cupuacu::AudioBuffer>();
        buffer->resize(channelCount, frameCount);
    }

    SampleFormat getSampleFormat() { return format; }

    int getSampleRate() const { return sampleRate; }
    int64_t getFrameCount() const { return buffer->getFrameCount(); }
    int64_t getChannelCount() const { return buffer->getChannelCount(); }

    float getSample(int64_t channel, int64_t frame) const { return buffer->getSample(channel, frame); }
    void setSample(int64_t channel, int64_t frame, float value, const bool shouldMarkDirty = true) { buffer->setSample(channel, frame, value, shouldMarkDirty); }

    void resizeBuffer(int64_t channels, int64_t frames) { buffer->resize(channels, frames); }
    void insertFrames(int64_t frameIndex, int64_t numFrames) { buffer->insertFrames(frameIndex, numFrames); }
    void removeFrames(int64_t frameIndex, int64_t numFrames) { buffer->removeFrames(frameIndex, numFrames); }

    const std::shared_ptr<cupuacu::AudioBuffer> getAudioBuffer() const { return buffer; }
};
}
