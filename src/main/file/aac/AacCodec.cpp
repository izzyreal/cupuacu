#include "AacCodec.hpp"

#define MAAC_COMPACT_CODEBOOKS
#include <maac.h>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace cupuacu::file::aac
{
    namespace
    {
        std::uint16_t channelCount(const maac_u8 configuration)
        {
            constexpr std::uint16_t counts[]{0, 1, 2, 3, 4, 5, 6, 8};
            return configuration < 8 ? counts[configuration] : 0;
        }
    } // namespace

    struct Decoder::Impl
    {
        maac_raw decoder{};
        maac_bitreader bitReader{};
        std::vector<maac_channel> channelBuffers;
        std::uint32_t configuredSampleRate = 0;
        std::uint16_t configuredChannels = 0;
    };

    Decoder::Decoder(const std::span<const std::uint8_t> audioSpecificConfig)
        : impl(std::make_unique<Impl>())
    {
        if (audioSpecificConfig.empty() ||
            audioSpecificConfig.size() > std::numeric_limits<maac_u32>::max())
        {
            throw std::runtime_error("Invalid AAC AudioSpecificConfig");
        }

        maac_raw_init(&impl->decoder);
        maac_bitreader_init(&impl->bitReader);
        if (maac_raw_config(
                &impl->decoder, audioSpecificConfig.data(),
                static_cast<maac_u32>(audioSpecificConfig.size())) != MAAC_OK)
        {
            throw std::runtime_error(
                "Unsupported AAC configuration (AAC-LC is required)");
        }

        impl->configuredSampleRate = impl->decoder.sample_rate;
        impl->configuredChannels =
            channelCount(impl->decoder.channel_configuration);
        if (impl->configuredSampleRate == 0 || impl->configuredChannels == 0 ||
            impl->configuredChannels > 8)
        {
            throw std::runtime_error("Unsupported AAC channel configuration");
        }

        impl->channelBuffers.resize(impl->configuredChannels);
        for (auto &channel : impl->channelBuffers)
        {
            maac_channel_init(&channel);
        }
        impl->decoder.out_channels = impl->channelBuffers.data();
        impl->decoder.num_out_channels =
            static_cast<maac_u8>(impl->configuredChannels);
    }

    Decoder::~Decoder() = default;
    Decoder::Decoder(Decoder &&) noexcept = default;
    Decoder &Decoder::operator=(Decoder &&) noexcept = default;

    std::uint32_t Decoder::sampleRate() const
    {
        return impl->configuredSampleRate;
    }

    std::uint16_t Decoder::channels() const
    {
        return impl->configuredChannels;
    }

    DecodedAacBlock Decoder::decode(const std::span<const std::uint8_t> packet)
    {
        if (packet.empty() ||
            packet.size() > std::numeric_limits<maac_u32>::max())
        {
            throw std::runtime_error("Invalid empty AAC packet");
        }

        impl->bitReader.data = packet.data();
        impl->bitReader.len = static_cast<maac_u32>(packet.size());
        impl->bitReader.pos = 0;
        const auto result = maac_raw_decode(&impl->decoder, &impl->bitReader);
        if (result != MAAC_OK)
        {
            throw std::runtime_error("Failed to decode AAC-LC packet (error " +
                                     std::to_string(result) + ")");
        }

        const auto frameCount = impl->channelBuffers.front().n_samples;
        if (frameCount == 0)
        {
            throw std::runtime_error("AAC-LC decoder produced no samples");
        }
        for (const auto &channel : impl->channelBuffers)
        {
            if (channel.n_samples != frameCount)
            {
                throw std::runtime_error(
                    "AAC-LC decoder produced inconsistent channel lengths");
            }
        }

        DecodedAacBlock decoded{
            .sampleRate = impl->configuredSampleRate,
            .channels = impl->configuredChannels,
            .frameCount = frameCount,
        };
        decoded.interleavedSamples.resize(static_cast<std::size_t>(frameCount) *
                                          impl->configuredChannels);
        for (std::uint32_t frame = 0; frame < frameCount; ++frame)
        {
            for (std::uint16_t channel = 0; channel < impl->configuredChannels;
                 ++channel)
            {
                const auto normalized = static_cast<float>(
                    impl->channelBuffers[channel].samples[frame] / 32768.0f);
                decoded.interleavedSamples[static_cast<std::size_t>(frame) *
                                               impl->configuredChannels +
                                           channel] =
                    std::clamp(normalized, -1.0f, 1.0f);
            }
        }
        return decoded;
    }
} // namespace cupuacu::file::aac
