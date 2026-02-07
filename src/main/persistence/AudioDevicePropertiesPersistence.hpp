#pragma once

#include "audio/AudioDevices.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace cupuacu::persistence
{
    class AudioDevicePropertiesPersistence
    {
    public:
        struct Resolver
        {
            std::function<std::string(int)> resolveHostApiName;
            std::function<std::string(int)> resolveDeviceName;
            std::function<int(const std::string &)> resolveHostApiIndex;
            std::function<int(const std::string &, bool, int)>
                resolveDeviceIndex;
        };

        static bool
        save(const std::filesystem::path &path,
             const cupuacu::audio::AudioDevices::DeviceSelection &selection);

        static std::optional<cupuacu::audio::AudioDevices::DeviceSelection>
        load(const std::filesystem::path &path);

        static void setResolverForTesting(Resolver resolver);
        static void resetResolverForTesting();
    };
} // namespace cupuacu::persistence
