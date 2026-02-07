#include "persistence/AudioDevicePropertiesPersistence.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

#include <portaudio.h>

namespace cupuacu::persistence
{
    namespace
    {
        constexpr int kFormatVersion = 1;
        std::optional<AudioDevicePropertiesPersistence::Resolver> gResolverOverride;

        bool ensurePortAudioInitialized(bool &initializedHere)
        {
            initializedHere = false;
            const int hostApiCount = Pa_GetHostApiCount();
            if (hostApiCount == paNotInitialized)
            {
                if (Pa_Initialize() != paNoError)
                {
                    return false;
                }
                initializedHere = true;
            }
            return true;
        }

        bool hasResolverOverride()
        {
            return gResolverOverride.has_value();
        }

        void terminatePortAudioIfNeeded(const bool initializedHere)
        {
            if (initializedHere)
            {
                Pa_Terminate();
            }
        }

        std::string resolveHostApiName(const int hostApiIndex)
        {
            if (hostApiIndex < 0)
            {
                return "";
            }
            const PaHostApiInfo *info = Pa_GetHostApiInfo(hostApiIndex);
            if (!info || !info->name)
            {
                return "";
            }
            return info->name;
        }

        std::string resolveDeviceName(const int deviceIndex)
        {
            if (deviceIndex < 0)
            {
                return "";
            }
            const PaDeviceInfo *info = Pa_GetDeviceInfo(deviceIndex);
            if (!info || !info->name)
            {
                return "";
            }
            return info->name;
        }

        int resolveHostApiIndex(const std::string &hostApiName)
        {
            if (hostApiName.empty())
            {
                return -1;
            }

            const int count = Pa_GetHostApiCount();
            if (count < 0)
            {
                return -1;
            }

            for (int i = 0; i < count; ++i)
            {
                const PaHostApiInfo *info = Pa_GetHostApiInfo(i);
                if (info && info->name && hostApiName == info->name)
                {
                    return i;
                }
            }

            return -1;
        }

        int resolveDeviceIndex(const std::string &deviceName, const bool isInput,
                               const int hostApiIndex)
        {
            if (deviceName.empty())
            {
                return -1;
            }

            const int count = Pa_GetDeviceCount();
            if (count < 0)
            {
                return -1;
            }

            const auto matches = [&](const PaDeviceInfo *info,
                                     const bool checkHostApi)
            {
                if (!info || !info->name)
                {
                    return false;
                }
                if (checkHostApi && hostApiIndex >= 0 &&
                    info->hostApi != hostApiIndex)
                {
                    return false;
                }
                if (isInput ? info->maxInputChannels <= 0
                            : info->maxOutputChannels <= 0)
                {
                    return false;
                }
                return deviceName == info->name;
            };

            for (int i = 0; i < count; ++i)
            {
                const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
                if (matches(info, true))
                {
                    return i;
                }
            }

            if (hostApiIndex >= 0)
            {
                for (int i = 0; i < count; ++i)
                {
                    const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
                    if (matches(info, false))
                    {
                        return i;
                    }
                }
            }

            return -1;
        }

        AudioDevicePropertiesPersistence::Resolver defaultPortAudioResolver()
        {
            AudioDevicePropertiesPersistence::Resolver resolver;
            resolver.resolveHostApiName = resolveHostApiName;
            resolver.resolveDeviceName = resolveDeviceName;
            resolver.resolveHostApiIndex = resolveHostApiIndex;
            resolver.resolveDeviceIndex = resolveDeviceIndex;
            return resolver;
        }

        const AudioDevicePropertiesPersistence::Resolver &currentResolver()
        {
            if (gResolverOverride.has_value())
            {
                return *gResolverOverride;
            }

            static const AudioDevicePropertiesPersistence::Resolver kDefault =
                defaultPortAudioResolver();
            return kDefault;
        }

        nlohmann::json serializeSelection(
            const cupuacu::audio::AudioDevices::DeviceSelection &selection,
            const AudioDevicePropertiesPersistence::Resolver &resolver)
        {
            return nlohmann::json{{"version", kFormatVersion},
                                  {"hostApiName",
                                   resolver.resolveHostApiName
                                       ? resolver.resolveHostApiName(
                                             selection.hostApiIndex)
                                       : std::string{}},
                                  {"outputDeviceName",
                                   resolver.resolveDeviceName
                                       ? resolver.resolveDeviceName(
                                             selection.outputDeviceIndex)
                                       : std::string{}},
                                  {"inputDeviceName",
                                   resolver.resolveDeviceName
                                       ? resolver.resolveDeviceName(
                                             selection.inputDeviceIndex)
                                       : std::string{}}};
        }

        std::optional<cupuacu::audio::AudioDevices::DeviceSelection>
        deserializeSelection(
            const nlohmann::json &json,
            const AudioDevicePropertiesPersistence::Resolver &resolver)
        {
            if (!json.is_object())
            {
                return std::nullopt;
            }

            if (!json.contains("version") || !json.contains("hostApiName") ||
                !json.contains("outputDeviceName") ||
                !json.contains("inputDeviceName"))
            {
                return std::nullopt;
            }

            const int version = json.at("version").get<int>();
            if (version != kFormatVersion)
            {
                return std::nullopt;
            }

            cupuacu::audio::AudioDevices::DeviceSelection selection;
            const std::string hostApiName = json.at("hostApiName").get<std::string>();
            const std::string outputDeviceName =
                json.at("outputDeviceName").get<std::string>();
            const std::string inputDeviceName =
                json.at("inputDeviceName").get<std::string>();

            selection.hostApiIndex = resolver.resolveHostApiIndex
                                         ? resolver.resolveHostApiIndex(hostApiName)
                                         : -1;
            selection.outputDeviceIndex = resolver.resolveDeviceIndex
                                              ? resolver.resolveDeviceIndex(
                                                    outputDeviceName, false,
                                                    selection.hostApiIndex)
                                              : -1;
            selection.inputDeviceIndex = resolver.resolveDeviceIndex
                                             ? resolver.resolveDeviceIndex(
                                                   inputDeviceName, true,
                                                   selection.hostApiIndex)
                                             : -1;

            return selection;
        }
    } // namespace

    bool AudioDevicePropertiesPersistence::save(
        const std::filesystem::path &path,
        const cupuacu::audio::AudioDevices::DeviceSelection &selection)
    {
        if (path.empty())
        {
            return false;
        }

        const auto &resolver = currentResolver();
        bool initializedHere = false;
        if (!hasResolverOverride() &&
            !ensurePortAudioInitialized(initializedHere))
        {
            return false;
        }

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            terminatePortAudioIfNeeded(initializedHere);
            return false;
        }

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            terminatePortAudioIfNeeded(initializedHere);
            return false;
        }

        out << serializeSelection(selection, resolver).dump(2);
        const bool ok = out.good();
        terminatePortAudioIfNeeded(initializedHere);
        return ok;
    }

    std::optional<cupuacu::audio::AudioDevices::DeviceSelection>
    AudioDevicePropertiesPersistence::load(const std::filesystem::path &path)
    {
        if (path.empty())
        {
            return std::nullopt;
        }
        if (!std::filesystem::exists(path))
        {
            return std::nullopt;
        }

        const auto &resolver = currentResolver();
        bool initializedHere = false;
        if (!hasResolverOverride() &&
            !ensurePortAudioInitialized(initializedHere))
        {
            return std::nullopt;
        }

        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            terminatePortAudioIfNeeded(initializedHere);
            return std::nullopt;
        }

        nlohmann::json json;
        try
        {
            in >> json;
            auto selection = deserializeSelection(json, resolver);
            terminatePortAudioIfNeeded(initializedHere);
            return selection;
        }
        catch (...)
        {
            terminatePortAudioIfNeeded(initializedHere);
            return std::nullopt;
        }
    }

    void AudioDevicePropertiesPersistence::setResolverForTesting(Resolver resolver)
    {
        gResolverOverride = std::move(resolver);
    }

    void AudioDevicePropertiesPersistence::resetResolverForTesting()
    {
        gResolverOverride.reset();
    }

} // namespace cupuacu::persistence
