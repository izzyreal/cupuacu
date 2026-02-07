#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace cupuacu::audio
{
    constexpr std::size_t kMaxRecordedChannels = 2;
    constexpr std::size_t kRecordedChunkFrames = 256;

    struct RecordedChunk
    {
        int64_t startFrame = 0;
        uint32_t frameCount = 0;
        uint8_t channelCount = 0;
        std::array<float, kRecordedChunkFrames * kMaxRecordedChannels>
            interleavedSamples{};
    };
} // namespace cupuacu::audio
