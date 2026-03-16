#pragma once

#include "../SelectedChannels.hpp"

#include <cstdint>

namespace cupuacu::audio
{
    struct AudioProcessContext
    {
        int64_t bufferStartFrame = 0;
        unsigned long frameCount = 0;
        uint64_t effectStartFrame = 0;
        uint64_t effectEndFrame = 0;
        cupuacu::SelectedChannels targetChannels =
            cupuacu::SelectedChannels::BOTH;
    };

    class AudioProcessor
    {
    public:
        virtual ~AudioProcessor() = default;

        virtual void process(float *interleavedStereo,
                             unsigned long frameCount,
                             const AudioProcessContext &context) const = 0;
    };
} // namespace cupuacu::audio
