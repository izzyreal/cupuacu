#pragma once

#include <cstdint>

namespace cupuacu
{
    struct AudioDeviceState
    {
        bool isPlaying = false;
        int64_t playbackPosition = -1;
    };
} // namespace cupuacu
