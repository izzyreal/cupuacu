#include "ClipboardAudio.hpp"
#include "storage/AudioEditRevision.hpp"
#include "concurrency/DeferredRelease.hpp"

namespace cupuacu
{
    void ClipboardAudio::assignRevision(
        std::shared_ptr<const storage::AudioEditRevision> value)
    {
        if (!value)
        {
            throw std::invalid_argument("Missing clipboard revision");
        }
        auto retained = concurrency::releaseOnWorker(std::move(value));
        auto old = segment ? concurrency::releaseOnWorker(segment) : nullptr;
        segment.reset();
        audioRevision = std::move(retained);
        touch();
    }
    SampleFormat ClipboardAudio::getSampleFormat() const
    {
        return audioRevision
                   ? audioRevision->shape().format
                   : (segment ? segment->format : SampleFormat::Unknown);
    }
    int ClipboardAudio::getSampleRate() const
    {
        return audioRevision ? audioRevision->shape().sampleRate
                             : (segment ? segment->sampleRate : 0);
    }
    int64_t ClipboardAudio::getFrameCount() const
    {
        return audioRevision ? audioRevision->shape().frames
                             : (segment ? segment->frameCount : 0);
    }
    int64_t ClipboardAudio::getChannelCount() const
    {
        return audioRevision ? audioRevision->shape().channels
                             : (segment ? segment->channelCount : 0);
    }
} // namespace cupuacu
