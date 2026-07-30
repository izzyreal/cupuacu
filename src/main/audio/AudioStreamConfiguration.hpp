#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cupuacu::audio
{
    enum class AudioStreamPurpose
    {
        Probe,
        Playback,
        Recording,
        Monitoring,
    };

    enum class AudioStreamFailureStage
    {
        Validation,
        FormatProbe,
        Open,
        Start,
        MonitorPreparation,
    };

    struct AudioStreamRequest
    {
        AudioStreamPurpose purpose = AudioStreamPurpose::Probe;
        int inputDeviceIndex = -1;
        int outputDeviceIndex = -1;
        double sampleRate = 0.0;
        uint8_t inputChannels = 0;
        uint8_t outputChannels = 0;

        [[nodiscard]] bool hasInput() const noexcept
        {
            return inputDeviceIndex >= 0 && inputChannels > 0;
        }

        [[nodiscard]] bool hasOutput() const noexcept
        {
            return outputDeviceIndex >= 0 && outputChannels > 0;
        }

        [[nodiscard]] bool isDuplex() const noexcept
        {
            return hasInput() && hasOutput();
        }
    };

    struct AudioStreamFailure
    {
        AudioStreamFailureStage stage = AudioStreamFailureStage::Validation;
        int portAudioError = 0;
        std::string message;
        std::string hostMessage;
        AudioStreamRequest request;
    };

    struct AudioStreamSetupResult
    {
        std::optional<AudioStreamFailure> failure;

        [[nodiscard]] bool succeeded() const noexcept
        {
            return !failure.has_value();
        }

        explicit operator bool() const noexcept
        {
            return succeeded();
        }
    };

    struct DeviceRateSupport
    {
        std::vector<double> mono;
        std::vector<double> stereo;
    };
} // namespace cupuacu::audio
