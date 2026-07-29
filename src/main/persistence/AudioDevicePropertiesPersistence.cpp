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
        constexpr int kFormatVersion = 2;
        std::optional<AudioDevicePropertiesPersistence::Resolver>
            gResolverOverride;

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

        bool selectionRequiresPortAudio(
            const cupuacu::audio::AudioDevices::DeviceSelection &selection)
        {
            return selection.hostApiIndex >= 0 ||
                   selection.outputDeviceIndex >= 0 ||
                   selection.inputDeviceIndex >= 0;
        }

        bool persistedNamesRequirePortAudio(const nlohmann::json &json)
        {
            const std::string hostApiName =
                json.value("hostApiName", std::string{});
            const std::string outputDeviceName =
                json.value("outputDeviceName", std::string{});
            const std::string inputDeviceName =
                json.value("inputDeviceName", std::string{});
            return !hostApiName.empty() || !outputDeviceName.empty() ||
                   !inputDeviceName.empty();
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

        int resolveDeviceIndex(const std::string &deviceName,
                               const bool isInput, const int hostApiIndex)
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

            const auto matches =
                [&](const PaDeviceInfo *info, const bool checkHostApi)
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

        std::string
        serializeMode(const cupuacu::audio::FeedbackSuppressionMode mode)
        {
            switch (mode)
            {
                case cupuacu::audio::FeedbackSuppressionMode::Off:
                    return "off";
                case cupuacu::audio::FeedbackSuppressionMode::Smooth:
                    return "smooth";
                case cupuacu::audio::FeedbackSuppressionMode::Standard:
                default:
                    return "standard";
            }
        }

        cupuacu::audio::FeedbackSuppressionMode
        deserializeMode(const nlohmann::json &json)
        {
            if (!json.contains("feedbackSuppressionMode"))
            {
                return cupuacu::audio::FeedbackSuppressionMode::Standard;
            }
            const auto value =
                json.at("feedbackSuppressionMode").get<std::string>();
            if (value == "off")
            {
                return cupuacu::audio::FeedbackSuppressionMode::Off;
            }
            if (value == "smooth")
            {
                return cupuacu::audio::FeedbackSuppressionMode::Smooth;
            }
            return cupuacu::audio::FeedbackSuppressionMode::Standard;
        }

        nlohmann::json serializeProperties(
            const AudioDevicePropertiesPersistence::Properties &properties,
            const AudioDevicePropertiesPersistence::Resolver &resolver)
        {
            const auto &selection = properties.deviceSelection;
            return nlohmann::json{
                {"version", kFormatVersion},
                {"feedbackSuppressionMode",
                 serializeMode(properties.feedbackSuppressionMode)},
                {"hostApiName",
                 resolver.resolveHostApiName
                     ? resolver.resolveHostApiName(selection.hostApiIndex)
                     : std::string{}},
                {"outputDeviceName",
                 resolver.resolveDeviceName
                     ? resolver.resolveDeviceName(selection.outputDeviceIndex)
                     : std::string{}},
                {"inputDeviceName",
                 resolver.resolveDeviceName
                     ? resolver.resolveDeviceName(selection.inputDeviceIndex)
                     : std::string{}}};
        }

        std::optional<AudioDevicePropertiesPersistence::Properties>
        deserializeProperties(
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
            if (version != 1 && version != kFormatVersion)
            {
                return std::nullopt;
            }

            cupuacu::audio::AudioDevices::DeviceSelection selection;
            const std::string hostApiName =
                json.at("hostApiName").get<std::string>();
            const std::string outputDeviceName =
                json.at("outputDeviceName").get<std::string>();
            const std::string inputDeviceName =
                json.at("inputDeviceName").get<std::string>();

            selection.hostApiIndex =
                resolver.resolveHostApiIndex
                    ? resolver.resolveHostApiIndex(hostApiName)
                    : -1;
            selection.outputDeviceIndex =
                resolver.resolveDeviceIndex
                    ? resolver.resolveDeviceIndex(outputDeviceName, false,
                                                  selection.hostApiIndex)
                    : -1;
            selection.inputDeviceIndex =
                resolver.resolveDeviceIndex
                    ? resolver.resolveDeviceIndex(inputDeviceName, true,
                                                  selection.hostApiIndex)
                    : -1;

            return AudioDevicePropertiesPersistence::Properties{
                .deviceSelection = selection,
                .feedbackSuppressionMode = deserializeMode(json)};
        }
    } // namespace

    bool
    AudioDevicePropertiesPersistence::save(const std::filesystem::path &path,
                                           const Properties &properties)
    {
        if (path.empty())
        {
            return false;
        }

        const auto &resolver = currentResolver();
        bool initializedHere = false;
        if (!hasResolverOverride() &&
            selectionRequiresPortAudio(properties.deviceSelection) &&
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

        out << serializeProperties(properties, resolver).dump(2);
        const bool ok = out.good();
        terminatePortAudioIfNeeded(initializedHere);
        return ok;
    }

    bool AudioDevicePropertiesPersistence::save(
        const std::filesystem::path &path,
        const cupuacu::audio::AudioDevices::DeviceSelection &selection)
    {
        return save(path, {.deviceSelection = selection});
    }

    std::optional<AudioDevicePropertiesPersistence::Properties>
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
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            return std::nullopt;
        }

        nlohmann::json json;
        try
        {
            in >> json;
            bool initializedHere = false;
            if (!hasResolverOverride() &&
                persistedNamesRequirePortAudio(json) &&
                !ensurePortAudioInitialized(initializedHere))
            {
                return std::nullopt;
            }
            auto properties = deserializeProperties(json, resolver);
            terminatePortAudioIfNeeded(initializedHere);
            return properties;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    void
    AudioDevicePropertiesPersistence::setResolverForTesting(Resolver resolver)
    {
        gResolverOverride = std::move(resolver);
    }

    void AudioDevicePropertiesPersistence::resetResolverForTesting()
    {
        gResolverOverride.reset();
    }

} // namespace cupuacu::persistence
