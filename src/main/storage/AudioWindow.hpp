#pragma once

#include "AudioReader.hpp"
#include <algorithm>
#include <vector>

namespace cupuacu::storage
{
    // Worker scratch is proportional to the requested window. Check obsolete
    // views between bounded reads, including when the backing cache is cold.
    template <typename CancelCheck>
    bool readAudioWindow(const AudioReader &reader, int channel, int64_t start,
                         std::span<float> output, const CancelCheck &canceled)
    {
        AudioReader::validateRange(reader.shape(), channel, start,
                                   output.size());
        constexpr std::size_t quantum = 65536;
        for (std::size_t offset = 0; offset < output.size(); offset += quantum)
        {
            if (canceled())
            {
                return false;
            }
            reader.readChannel(
                channel, start + int64_t(offset),
                output.subspan(offset,
                               std::min(quantum, output.size() - offset)));
        }
        return !canceled();
    }
} // namespace cupuacu::storage
