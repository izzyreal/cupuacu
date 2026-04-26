#include <catch2/catch_test_macros.hpp>

#include "file/alac/AlacCodec.hpp"
#include "file/m4a/M4aAlacReader.hpp"
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

    cupuacu::file::m4a::Bytes customSampleToChunkAtom(
        const std::vector<std::pair<std::uint32_t, std::uint32_t>> &entries)
    {
        cupuacu::file::m4a::Bytes payload;
        cupuacu::file::m4a::appendBe32(
            payload, static_cast<std::uint32_t>(entries.size()));
        for (const auto &[firstChunk, samplesPerChunk] : entries)
        {
            cupuacu::file::m4a::appendBe32(payload, firstChunk);
            cupuacu::file::m4a::appendBe32(payload, samplesPerChunk);
            cupuacu::file::m4a::appendBe32(payload, 1);
        }
        return cupuacu::file::m4a::fullAtom("stsc", 0, 0, payload);
    }

    cupuacu::file::m4a::Bytes makeMultiChunkAlacM4a(
        const cupuacu::file::alac::AlacEncodedPackets &encoded)
    {
        REQUIRE(encoded.packetSizes.size() == 3);

        using namespace cupuacu::file::m4a;
        const auto ftyp = ftypAtom();
        const auto mdat = mdatAtom(encoded.bytes);
        const auto mdatPayloadOffset =
            static_cast<std::uint32_t>(ftyp.size() + 8u);
        const auto chunkOffsets = std::vector<std::uint32_t>{
            mdatPayloadOffset,
            static_cast<std::uint32_t>(mdatPayloadOffset +
                                       encoded.packetSizes[0]),
        };

        const auto sampleEntry = alacSampleEntry({
            .channels = encoded.cookie.channels,
            .bitDepth = encoded.cookie.bitDepth,
            .sampleRate = encoded.cookie.sampleRate,
            .magicCookie = encoded.cookie.bytes,
        });
        const auto stbl = containerAtom(
            "stbl",
            {sampleDescriptionAtom({sampleEntry}),
             timeToSampleAtom(encoded.frameCount, encoded.framesPerPacket),
             customSampleToChunkAtom({{1u, 1u}, {2u, 2u}}),
             sampleSizeAtom(encoded.packetSizes),
             chunkOffsetAtom(chunkOffsets)});
        const auto minf =
            containerAtom("minf",
                          {soundMediaHeaderAtom(), dataInformationAtom(), stbl});
        const auto mdia =
            containerAtom("mdia",
                          {mediaHeaderAtom(encoded.cookie.sampleRate,
                                           encoded.frameCount),
                           handlerReferenceAtom("soun"), minf});
        const auto trak = containerAtom(
            "trak", {trackHeaderAtom(1, encoded.frameCount, true), mdia});
        const auto moov = containerAtom(
            "moov", {movieHeaderAtom(encoded.cookie.sampleRate,
                                     encoded.frameCount, 2u),
                     trak});

        Bytes file;
        file.reserve(ftyp.size() + mdat.size() + moov.size());
        file.insert(file.end(), ftyp.begin(), ftyp.end());
        file.insert(file.end(), mdat.begin(), mdat.end());
        file.insert(file.end(), moov.begin(), moov.end());
        return file;
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

TEST_CASE("M4A parser and reader support ALAC packets split across chunks",
          "[m4a]")
{
    const auto frames =
        cupuacu::file::alac::defaultFramesPerPacket() * 2u + 1u;
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
    REQUIRE(encoded->packetSizes.size() == 3);

    const auto bytes = makeMultiChunkAlacM4a(*encoded);
    const auto parsed = cupuacu::file::m4a::parseAlacM4a(bytes);

    REQUIRE(parsed.packetSizes == encoded->packetSizes);
    REQUIRE(parsed.packetOffsets.size() == 3);
    REQUIRE(parsed.packetOffsets[0] == parsed.mdatPayloadOffset);
    REQUIRE(parsed.packetOffsets[1] ==
            parsed.mdatPayloadOffset + encoded->packetSizes[0]);
    REQUIRE(parsed.packetOffsets[2] ==
            parsed.mdatPayloadOffset + encoded->packetSizes[0] +
                encoded->packetSizes[1]);

    const auto decoded = cupuacu::file::m4a::readAlacM4a(bytes);
    REQUIRE(decoded.channels == 2);
    REQUIRE(decoded.sampleRate == 44100);
    REQUIRE(decoded.bitDepth == 16);
    REQUIRE(decoded.frameCount == frames);
    REQUIRE(decoded.interleavedPcmBytes.size() == pcm.size());
    for (std::size_t i = 0; i < pcm.size(); ++i)
    {
        REQUIRE(decoded.interleavedPcmBytes[i] == pcm[i]);
    }
}
