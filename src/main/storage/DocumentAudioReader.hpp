#pragma once
#include "AudioReader.hpp"
#include "../Document.hpp"

namespace cupuacu::storage
{
    // Pins the existing copy-on-write revision while consumers migrate.
    class DocumentAudioReader final : public AudioReader
    {
        Document document;

    public:
        explicit DocumentAudioReader(const Document &source) : document(source)
        {
        }
        AudioShape shape() const override
        {
            return {document.getFrameCount(), int(document.getChannelCount()),
                    document.getSampleRate(), document.getSampleFormat()};
        }
        void readChannel(int channel, int64_t start,
                         std::span<float> destination) const override
        {
            validateRange(shape(), channel, start, destination.size());
            auto lease = document.acquireReadLease();
            lease.readChannelFloatBlock(channel, start, destination.data(),
                                        destination.size());
        }
    };
} // namespace cupuacu::storage
