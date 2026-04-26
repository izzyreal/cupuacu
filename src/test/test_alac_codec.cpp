#include <catch2/catch_test_macros.hpp>

#include "file/alac/AlacCodec.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
    std::uint32_t readBe32(const std::vector<std::uint8_t> &bytes,
                           const std::size_t offset)
    {
        return (static_cast<std::uint32_t>(bytes[offset]) << 24u) |
               (static_cast<std::uint32_t>(bytes[offset + 1]) << 16u) |
               (static_cast<std::uint32_t>(bytes[offset + 2]) << 8u) |
               static_cast<std::uint32_t>(bytes[offset + 3]);
    }

    void appendLe16(std::vector<std::uint8_t> &bytes, const std::int16_t value)
    {
        const auto unsignedValue = static_cast<std::uint16_t>(value);
        bytes.push_back(static_cast<std::uint8_t>(unsignedValue & 0xffu));
        bytes.push_back(static_cast<std::uint8_t>((unsignedValue >> 8u) & 0xffu));
    }

    std::vector<std::uint8_t> makeStereoPcm16(const std::uint32_t frames)
    {
        std::vector<std::uint8_t> bytes;
        for (std::uint32_t frame = 0; frame < frames; ++frame)
        {
            appendLe16(bytes, static_cast<std::int16_t>(frame * 100));
            appendLe16(bytes, static_cast<std::int16_t>(-100 - frame * 100));
        }
        return bytes;
    }

    std::vector<std::int16_t> readNativeI16Samples(
        const std::vector<std::uint8_t> &bytes)
    {
        std::vector<std::int16_t> samples(bytes.size() / sizeof(std::int16_t));
        std::memcpy(samples.data(), bytes.data(), bytes.size());
        return samples;
    }

} // namespace

TEST_CASE("ALAC codec exposes stable encoder limits", "[alac]")
{
    REQUIRE(cupuacu::file::alac::defaultFramesPerPacket() == 4096);
    REQUIRE(cupuacu::file::alac::maxChannels() == 8);
}

TEST_CASE("ALAC codec validates supported encoder parameters", "[alac]")
{
    using cupuacu::file::alac::AlacEncodingParameters;
    using cupuacu::file::alac::defaultFramesPerPacket;
    using cupuacu::file::alac::isSupportedEncoding;

    REQUIRE(isSupportedEncoding(AlacEncodingParameters{
        .sampleRate = 48000,
        .channels = 2,
        .bitsPerSample = 24,
        .framesPerPacket = defaultFramesPerPacket(),
    }));
    REQUIRE(isSupportedEncoding(AlacEncodingParameters{
        .sampleRate = 96000,
        .channels = 8,
        .bitsPerSample = 32,
        .framesPerPacket = defaultFramesPerPacket(),
    }));

    REQUIRE_FALSE(isSupportedEncoding(AlacEncodingParameters{
        .sampleRate = 0,
        .channels = 2,
        .bitsPerSample = 24,
        .framesPerPacket = defaultFramesPerPacket(),
    }));
    REQUIRE_FALSE(isSupportedEncoding(AlacEncodingParameters{
        .sampleRate = 48000,
        .channels = 9,
        .bitsPerSample = 24,
        .framesPerPacket = defaultFramesPerPacket(),
    }));
    REQUIRE_FALSE(isSupportedEncoding(AlacEncodingParameters{
        .sampleRate = 48000,
        .channels = 2,
        .bitsPerSample = 12,
        .framesPerPacket = defaultFramesPerPacket(),
    }));
    REQUIRE_FALSE(isSupportedEncoding(AlacEncodingParameters{
        .sampleRate = 48000,
        .channels = 2,
        .bitsPerSample = 16,
        .framesPerPacket = 2,
    }));
}

TEST_CASE("ALAC codec creates an encoder cookie for valid parameters", "[alac]")
{
    const auto cookie = cupuacu::file::alac::makeEncoderCookie({
        .sampleRate = 44100,
        .channels = 2,
        .bitsPerSample = 16,
    });

    REQUIRE(cookie.has_value());
    REQUIRE(cookie->bytes.size() >= 24);
    REQUIRE(cookie->frameLength ==
            cupuacu::file::alac::defaultFramesPerPacket());
    REQUIRE(cookie->bitDepth == 16);
    REQUIRE(cookie->channels == 2);
    REQUIRE(cookie->sampleRate == 44100);
    REQUIRE(readBe32(cookie->bytes, 0) ==
            cupuacu::file::alac::defaultFramesPerPacket());
    REQUIRE(readBe32(cookie->bytes, 20) == 44100);
}

TEST_CASE("ALAC codec rejects invalid encoder cookies", "[alac]")
{
    const auto cookie = cupuacu::file::alac::makeEncoderCookie({
        .sampleRate = 44100,
        .channels = 0,
        .bitsPerSample = 16,
    });

    REQUIRE_FALSE(cookie.has_value());
}

TEST_CASE("ALAC codec packetizes complete PCM frames", "[alac]")
{
    const auto frames = cupuacu::file::alac::defaultFramesPerPacket() * 2;
    const auto pcm = makeStereoPcm16(frames);
    const auto encoded = cupuacu::file::alac::encodePcmPackets(
        {
            .sampleRate = 44100,
            .channels = 2,
            .bitsPerSample = 16,
            .framesPerPacket = cupuacu::file::alac::defaultFramesPerPacket(),
        },
        pcm);

    REQUIRE(encoded.has_value());
    REQUIRE(encoded->frameCount == frames);
    REQUIRE(encoded->framesPerPacket ==
            cupuacu::file::alac::defaultFramesPerPacket());
    REQUIRE(encoded->packetSizes.size() == 2);
    REQUIRE(encoded->bytes.size() ==
            encoded->packetSizes[0] + encoded->packetSizes[1]);
    REQUIRE_FALSE(encoded->bytes.empty());
    REQUIRE(encoded->cookie.bitDepth == 16);
    REQUIRE(encoded->cookie.channels == 2);
    REQUIRE(encoded->cookie.sampleRate == 44100);
}

TEST_CASE("ALAC codec packetizes final partial PCM frame group", "[alac]")
{
    const auto frames = cupuacu::file::alac::defaultFramesPerPacket() + 1;
    const auto encoded = cupuacu::file::alac::encodePcmPackets(
        {
            .sampleRate = 48000,
            .channels = 2,
            .bitsPerSample = 16,
            .framesPerPacket = cupuacu::file::alac::defaultFramesPerPacket(),
        },
        makeStereoPcm16(frames));

    REQUIRE(encoded.has_value());
    REQUIRE(encoded->frameCount == frames);
    REQUIRE(encoded->packetSizes.size() == 2);
}

TEST_CASE("ALAC codec rejects incomplete PCM frames", "[alac]")
{
    auto pcm = makeStereoPcm16(2);
    pcm.push_back(0xff);

    const auto encoded = cupuacu::file::alac::encodePcmPackets(
        {
            .sampleRate = 44100,
            .channels = 2,
            .bitsPerSample = 16,
            .framesPerPacket = cupuacu::file::alac::defaultFramesPerPacket(),
        },
        pcm);

    REQUIRE_FALSE(encoded.has_value());
}

TEST_CASE("ALAC codec decodes encoded PCM packets", "[alac]")
{
    const auto frames = cupuacu::file::alac::defaultFramesPerPacket() + 1;
    const auto pcm = makeStereoPcm16(frames);
    const auto encoded = cupuacu::file::alac::encodePcmPackets(
        {
            .sampleRate = 44100,
            .channels = 2,
            .bitsPerSample = 16,
            .framesPerPacket = cupuacu::file::alac::defaultFramesPerPacket(),
        },
        pcm);
    REQUIRE(encoded.has_value());
    REQUIRE(encoded->packetSizes.size() == 2);
    std::vector<std::uint32_t> progressFrames;

    const auto decoded = cupuacu::file::alac::decodePcm16Packets(
        {
            .sampleRate = encoded->cookie.sampleRate,
            .channels = encoded->cookie.channels,
            .bitsPerSample = encoded->cookie.bitDepth,
            .framesPerPacket = encoded->framesPerPacket,
            .frameCount = encoded->frameCount,
            .magicCookie = encoded->cookie.bytes,
            .packetFrameCounts =
                {cupuacu::file::alac::defaultFramesPerPacket(), 1},
        },
        encoded->bytes, encoded->packetSizes,
        [&progressFrames](const std::uint32_t decodedFrames,
                          const std::uint32_t)
        {
            progressFrames.push_back(decodedFrames);
        });

    REQUIRE(decoded.has_value());
    REQUIRE(decoded->sampleRate == 44100);
    REQUIRE(decoded->channels == 2);
    REQUIRE(decoded->frameCount == frames);
    REQUIRE(decoded->interleavedSamples == readNativeI16Samples(pcm));
    REQUIRE(progressFrames ==
            std::vector<std::uint32_t>{
                cupuacu::file::alac::defaultFramesPerPacket(), frames});
}

TEST_CASE("ALAC codec decodes packets from scattered source offsets", "[alac]")
{
    const auto frames = cupuacu::file::alac::defaultFramesPerPacket() + 1;
    const auto pcm = makeStereoPcm16(frames);
    const auto encoded = cupuacu::file::alac::encodePcmPackets(
        {
            .sampleRate = 44100,
            .channels = 2,
            .bitsPerSample = 16,
            .framesPerPacket = cupuacu::file::alac::defaultFramesPerPacket(),
        },
        pcm);
    REQUIRE(encoded.has_value());
    REQUIRE(encoded->packetSizes.size() == 2);

    std::vector<std::uint8_t> sourceBytes{0xaa, 0xbb, 0xcc};
    std::vector<std::uint64_t> packetOffsets;
    packetOffsets.reserve(encoded->packetSizes.size());
    for (std::size_t i = 0; i < encoded->packetSizes.size(); ++i)
    {
        packetOffsets.push_back(sourceBytes.size());
        const auto packetStart = encoded->bytes.begin() +
                                 static_cast<std::ptrdiff_t>(
                                     i == 0 ? 0 : encoded->packetSizes[0]);
        const auto packetEnd =
            packetStart +
            static_cast<std::ptrdiff_t>(encoded->packetSizes[i]);
        sourceBytes.insert(sourceBytes.end(), packetStart, packetEnd);
        sourceBytes.push_back(0xee);
    }

    const auto decoded = cupuacu::file::alac::decodePcm16Packets(
        {
            .sampleRate = encoded->cookie.sampleRate,
            .channels = encoded->cookie.channels,
            .bitsPerSample = encoded->cookie.bitDepth,
            .framesPerPacket = encoded->framesPerPacket,
            .frameCount = encoded->frameCount,
            .magicCookie = encoded->cookie.bytes,
            .packetFrameCounts =
                {cupuacu::file::alac::defaultFramesPerPacket(), 1},
        },
        sourceBytes, packetOffsets, encoded->packetSizes);

    REQUIRE(decoded.has_value());
    REQUIRE(decoded->interleavedSamples == readNativeI16Samples(pcm));
}

TEST_CASE("ALAC codec decodes packets via packet reader callback", "[alac]")
{
    const auto frames = cupuacu::file::alac::defaultFramesPerPacket() + 1;
    const auto pcm = makeStereoPcm16(frames);
    const auto encoded = cupuacu::file::alac::encodePcmPackets(
        {
            .sampleRate = 44100,
            .channels = 2,
            .bitsPerSample = 16,
            .framesPerPacket = cupuacu::file::alac::defaultFramesPerPacket(),
        },
        pcm);
    REQUIRE(encoded.has_value());
    REQUIRE(encoded->packetSizes.size() == 2);

    std::vector<std::uint8_t> sourceBytes{0x10, 0x20, 0x30};
    std::vector<std::uint64_t> packetOffsets;
    packetOffsets.reserve(encoded->packetSizes.size());
    std::size_t packetStartOffset = 0;
    for (const auto packetSize : encoded->packetSizes)
    {
        packetOffsets.push_back(sourceBytes.size());
        sourceBytes.insert(
            sourceBytes.end(),
            encoded->bytes.begin() +
                static_cast<std::ptrdiff_t>(packetStartOffset),
            encoded->bytes.begin() +
                static_cast<std::ptrdiff_t>(packetStartOffset + packetSize));
        packetStartOffset += packetSize;
        sourceBytes.push_back(0xff);
    }

    const auto decoded = cupuacu::file::alac::decodePcmPackets(
        {
            .sampleRate = encoded->cookie.sampleRate,
            .channels = encoded->cookie.channels,
            .bitsPerSample = encoded->cookie.bitDepth,
            .framesPerPacket = encoded->framesPerPacket,
            .frameCount = encoded->frameCount,
            .magicCookie = encoded->cookie.bytes,
            .packetFrameCounts =
                {cupuacu::file::alac::defaultFramesPerPacket(), 1},
        },
        packetOffsets, encoded->packetSizes,
        [&sourceBytes](const std::uint64_t packetOffset,
                       const std::uint32_t packetSize,
                       std::vector<std::uint8_t> &packetBytes)
        {
            if (packetOffset > sourceBytes.size() ||
                packetSize > sourceBytes.size() - packetOffset)
            {
                return false;
            }

            std::copy(sourceBytes.begin() +
                          static_cast<std::ptrdiff_t>(packetOffset),
                      sourceBytes.begin() +
                          static_cast<std::ptrdiff_t>(packetOffset + packetSize),
                      packetBytes.begin());
            return true;
        });

    REQUIRE(decoded.has_value());
    REQUIRE(decoded->interleavedPcmBytes == pcm);
}
