#pragma once

#include <cstdint>

namespace cupuacu::audio
{
    struct SampleProvenance
    {
        std::uint64_t sourceId = 0;
        std::int64_t frameIndex = -1;

        [[nodiscard]] bool isValid() const
        {
            return sourceId != 0 && frameIndex >= 0;
        }
    };
} // namespace cupuacu::audio
