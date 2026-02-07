#pragma once

#include <memory>
#include <mutex>

namespace cupuacu::audio
{
    class AudioDevice;

    using AudioDevicePtr = std::shared_ptr<AudioDevice>;

    class AudioDevices
    {
    public:
        struct DeviceSelection
        {
            int hostApiIndex = -1;
            int outputDeviceIndex = -1;
            int inputDeviceIndex = -1;

            bool operator==(const DeviceSelection &other) const noexcept
            {
                return hostApiIndex == other.hostApiIndex &&
                       outputDeviceIndex == other.outputDeviceIndex &&
                       inputDeviceIndex == other.inputDeviceIndex;
            }
            bool operator!=(const DeviceSelection &other) const noexcept
            {
                return !(*this == other);
            }
        };

        AudioDevices();
        ~AudioDevices();

        AudioDevicePtr getOutputDevice();
        DeviceSelection getDeviceSelection() const;
        void setDeviceSelection(const DeviceSelection &selection);

    private:
        AudioDevicePtr outputDevice;
        mutable std::mutex selectionMutex;
        DeviceSelection deviceSelection;
    };
} // namespace cupuacu::audio
