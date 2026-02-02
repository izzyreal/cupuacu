#pragma once

#include <cstdint>

namespace cupuacu
{
    class AudioDeviceState;
    class AudioDeviceView
    {
    public:
        explicit AudioDeviceView(const AudioDeviceState *) noexcept;

        bool isPlaying() const;
        int64_t getPlaybackPosition() const;

    private:
        const AudioDeviceState *state;
    };
} // namespace cupuacu
