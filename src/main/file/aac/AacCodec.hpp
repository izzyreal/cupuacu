#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace cupuacu::file::aac
{
    struct DecodedAacBlock
    {
        std::uint32_t sampleRate = 0;
        std::uint16_t channels = 0;
        std::uint32_t frameCount = 0;
        std::vector<float> interleavedSamples;
    };

    class Decoder
    {
    public:
        explicit Decoder(std::span<const std::uint8_t> audioSpecificConfig);
        ~Decoder();
        Decoder(Decoder &&) noexcept;
        Decoder &operator=(Decoder &&) noexcept;
        Decoder(const Decoder &) = delete;
        Decoder &operator=(const Decoder &) = delete;

        [[nodiscard]] std::uint32_t sampleRate() const;
        [[nodiscard]] std::uint16_t channels() const;
        [[nodiscard]] DecodedAacBlock
        decode(std::span<const std::uint8_t> packet);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace cupuacu::file::aac
