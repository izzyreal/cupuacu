#pragma once

#include <memory>

namespace cupuacu
{
    class AudioDevice;

    using AudioDevicePtr = std::shared_ptr<AudioDevice>;

    class AudioDevices
    {
    public:
        AudioDevices();
        ~AudioDevices();

        AudioDevicePtr getOutputDevice();

    private:
        AudioDevicePtr outputDevice;
    };
} // namespace cupuacu
