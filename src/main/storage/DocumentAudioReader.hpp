#pragma once
#include "AudioReader.hpp"
#include "../Document.hpp"

namespace cupuacu::storage
{
    // Pins the existing copy-on-write revision while consumers migrate.
    class DocumentAudioReader final : public AudioReader
    {
        std::shared_ptr<const audio::AudioBuffer> buffer;
        AudioShape dimensions;

    public:
        explicit DocumentAudioReader(const Document &source)
        {
            auto lease = source.acquireReadLease();
            initialize(lease);
        }
        explicit DocumentAudioReader(const Document::ReadLease &lease)
        {
            initialize(lease);
        }

    private:
        void initialize(const Document::ReadLease &lease)
        {
            dimensions = {lease.getFrameCount(), int(lease.getChannelCount()),
                          lease.getSampleRate(), lease.getSampleFormat()};
            buffer = lease.snapshotAudioBuffer();
        }

    public:
        AudioShape shape() const override
        {
            return dimensions;
        }
        void readChannel(int channel, int64_t start,
                         std::span<float> destination) const override
        {
            validateRange(shape(), channel, start, destination.size());
            if (!destination.empty())
            {
                buffer->readChannelSamples(channel, start, destination.data(),
                                           destination.size(), 1);
            }
        }
    };
} // namespace cupuacu::storage
