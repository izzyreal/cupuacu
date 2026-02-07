#pragma once

#include <cstdint>

namespace cupuacu::audio
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
} // namespace cupuacu::audio
