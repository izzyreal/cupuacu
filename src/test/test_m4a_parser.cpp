#include <catch2/catch_test_macros.hpp>

#include "file/alac/AlacCodec.hpp"
#include "file/m4a/M4aAtoms.hpp"
#include "file/m4a/M4aParser.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace
{
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
            appendLe16(bytes, static_cast<std::int16_t>(100 + frame * 100));
            appendLe16(bytes, static_cast<std::int16_t>(-100 - frame * 100));
        }
        return bytes;
    }
} // namespace

TEST_CASE("M4A parser extracts ALAC packet tables from Cupuacu files", "[m4a]")
{
    const auto frames = cupuacu::file::alac::defaultFramesPerPacket() + 1;
    const auto encoded = cupuacu::file::alac::encodePcmPackets(
        {
            .sampleRate = 44100,
            .channels = 2,
            .bitsPerSample = 16,
            .framesPerPacket = cupuacu::file::alac::defaultFramesPerPacket(),
        },
        makeStereoPcm16(frames));
    REQUIRE(encoded.has_value());

    const auto bytes = cupuacu::file::m4a::assembleAlacM4a(*encoded);
    const auto parsed = cupuacu::file::m4a::parseAlacM4a(bytes);

    REQUIRE(parsed.sampleRate == 44100);
    REQUIRE(parsed.channels == 2);
    REQUIRE(parsed.bitDepth == 16);
    REQUIRE(parsed.frameCount == frames);
    REQUIRE(parsed.framesPerPacket ==
            cupuacu::file::alac::defaultFramesPerPacket());
    REQUIRE(parsed.packetSizes == encoded->packetSizes);
    REQUIRE(parsed.packetFrameCounts ==
            std::vector<std::uint32_t>{
                cupuacu::file::alac::defaultFramesPerPacket(), 1});
    REQUIRE(parsed.magicCookie == encoded->cookie.bytes);
    REQUIRE(parsed.mdatPayloadOffset == 36);
    REQUIRE(parsed.mdatPayloadSize == encoded->bytes.size());
    REQUIRE(parsed.packetOffsets.size() == encoded->packetSizes.size());
    REQUIRE(parsed.packetOffsets.front() == parsed.mdatPayloadOffset);

    std::uint64_t expectedOffset = parsed.mdatPayloadOffset;
    for (std::size_t i = 0; i < parsed.packetOffsets.size(); ++i)
    {
        REQUIRE(parsed.packetOffsets[i] == expectedOffset);
        expectedOffset += parsed.packetSizes[i];
    }
}

TEST_CASE("M4A parser rejects files without ALAC media data", "[m4a]")
{
    REQUIRE_THROWS_AS(cupuacu::file::m4a::parseAlacM4a(
                          cupuacu::file::m4a::ftypAtom()),
                      std::runtime_error);
}
