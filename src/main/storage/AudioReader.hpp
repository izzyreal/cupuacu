#pragma once

#include "../SampleFormat.hpp"
#include <cstdint>
#include <span>
#include <stdexcept>
#include <algorithm>

namespace cupuacu::storage
{
    struct AudioShape
    {
        int64_t frames = 0;
        int channels = 0;
        int sampleRate = 0;
        SampleFormat format = SampleFormat::Unknown;
    };

    // Explicitly blocking, worker-only range access. Implementations may read
    // disk and acquire locks. Never call from painting or an audio callback.
    class AudioReader
    {
    public:
        virtual ~AudioReader() = default;
        virtual AudioShape shape() const = 0;
        virtual void readChannel(int channel, int64_t start,
                                 std::span<float> destination) const = 0;
        virtual void readDirtyFlags(int channel, int64_t start,
                                    std::span<uint8_t> destination) const
        {
            validateRange(shape(), channel, start, destination.size());
            std::fill(destination.begin(), destination.end(), 1);
        }

        static void validateRange(AudioShape shape, int channel, int64_t start,
                                  std::size_t count)
        {
            if (channel < 0 || channel >= shape.channels || start < 0 ||
                start > shape.frames || count > uint64_t(shape.frames - start))
            {
                throw std::out_of_range("Audio read outside revision");
            }
        }
    };
} // namespace cupuacu::storage
