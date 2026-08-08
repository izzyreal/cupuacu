#include <catch2/catch_test_macros.hpp>

#include "file/aac/AacCodec.hpp"
#include "file/alac/AlacCodec.hpp"
#include "file/m4a/M4aAacReader.hpp"
#include "file/m4a/M4aAlacReader.hpp"
#include "file/m4a/M4aAtoms.hpp"
#include "file/m4a/M4aParser.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

    cupuacu::file::m4a::Bytes makeAacLcSampleEntry()
    {
        using namespace cupuacu::file::m4a;
        Bytes esDescriptor{
            0x03, 0x80, 0x80, 0x80, 0x22, 0x00, 0x01, 0x00, 0x04, 0x80,
            0x80, 0x80, 0x14, 0x40, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x16, 0x58, 0x00, 0x00, 0x0c, 0x15, 0x05, 0x80, 0x80, 0x80,
            0x02, 0x11, 0x88, 0x06, 0x80, 0x80, 0x80, 0x01, 0x02,
        };
        const auto esds = fullAtom("esds", 0, 0, esDescriptor);

        Bytes payload(6, 0);
        appendBe16(payload, 1); // data reference index
        payload.insert(payload.end(), 8, 0);
        appendBe16(payload, 1);  // mono
        appendBe16(payload, 16); // legacy sample size
        appendBe16(payload, 0);
        appendBe16(payload, 0);
        appendBe32(payload, 48000u << 16u);
        payload.insert(payload.end(), esds.begin(), esds.end());
        return atom("mp4a", payload);
    }

    cupuacu::file::m4a::Bytes makeAacLcM4a()
    {
        using namespace cupuacu::file::m4a;
        const Bytes packets{
            0xde, 0x02, 0x00, 0x4c, 0x61, 0x76, 0x63, 0x36, 0x32, 0x2e, 0x31,
            0x31, 0x2e, 0x31, 0x30, 0x30, 0x00, 0x02, 0x30, 0x40, 0x0e, 0x01,
            0x18, 0x20, 0x07, 0x01, 0x18, 0x20, 0x07, 0x01, 0x18, 0x20, 0x07,
        };
        const std::vector<std::uint32_t> packetSizes{21, 4, 4, 4};
        const auto ftyp = ftypAtom();
        const auto mdat = mdatAtom(packets);
        const auto payloadOffset = static_cast<std::uint32_t>(ftyp.size() + 8u);
        const auto stbl = containerAtom(
            "stbl",
            {sampleDescriptionAtom({makeAacLcSampleEntry()}),
             timeToSampleAtom(4096, 1024), sampleToChunkAtom(4),
             sampleSizeAtom(packetSizes), chunkOffsetAtom({payloadOffset})});
        const auto minf = containerAtom(
            "minf", {soundMediaHeaderAtom(), dataInformationAtom(), stbl});
        const auto mdia =
            containerAtom("mdia", {mediaHeaderAtom(48000, 4096),
                                   handlerReferenceAtom("soun"), minf});
        const auto trak =
            containerAtom("trak", {trackHeaderAtom(1, 4096, true), mdia});
        const auto moov =
            containerAtom("moov", {movieHeaderAtom(48000, 4096, 2), trak});

        Bytes file;
        file.insert(file.end(), ftyp.begin(), ftyp.end());
        file.insert(file.end(), mdat.begin(), mdat.end());
        file.insert(file.end(), moov.begin(), moov.end());
        return file;
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

TEST_CASE("M4A parser and Miniaac decode AAC-LC packets", "[m4a][aac]")
{
    const auto bytes = makeAacLcM4a();
    REQUIRE(cupuacu::file::m4a::detectM4aAudioCodec(bytes) ==
            cupuacu::file::m4a::M4aAudioCodec::AAC_LC);

    const auto parsed = cupuacu::file::m4a::parseAacM4a(bytes);
    REQUIRE(parsed.sampleRate == 48000);
    REQUIRE(parsed.channels == 1);
    REQUIRE(parsed.frameCount == 4096);
    REQUIRE(parsed.framesPerPacket == 1024);
    REQUIRE(parsed.audioSpecificConfig ==
            std::vector<std::uint8_t>{0x11, 0x88});
    REQUIRE(parsed.packetSizes == std::vector<std::uint32_t>{21, 4, 4, 4});

    cupuacu::file::aac::Decoder decoder(parsed.audioSpecificConfig);
    std::uint32_t decodedFrames = 0;
    for (std::size_t i = 0; i < parsed.packetSizes.size(); ++i)
    {
        const auto offset = static_cast<std::size_t>(parsed.packetOffsets[i]);
        const auto packetSize = static_cast<std::size_t>(parsed.packetSizes[i]);
        const auto decoded = decoder.decode(
            std::span<const std::uint8_t>(bytes).subspan(offset, packetSize));
        REQUIRE(decoded.sampleRate == 48000);
        REQUIRE(decoded.channels == 1);
        REQUIRE(decoded.frameCount == 1024);
        decodedFrames += decoded.frameCount;
    }
    REQUIRE(decodedFrames == parsed.frameCount);
}

TEST_CASE("M4A AAC reader streams decoded float samples", "[m4a][aac]")
{
    const auto bytes = makeAacLcM4a();
    const auto path = std::filesystem::temp_directory_path() /
                      "cupuacu-aac-lc-reader-test.m4a";
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup()
        {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } cleanup{path};
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        REQUIRE(output.good());
        output.write(reinterpret_cast<const char *>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        REQUIRE(output.good());
    }

    std::uint32_t streamedFrames = 0;
    const auto info = cupuacu::file::m4a::streamAacM4aFile(
        path,
        [&](const float *samples, const std::uint32_t frames,
            const std::uint16_t channels)
        {
            REQUIRE(samples != nullptr);
            REQUIRE(channels == 1);
            streamedFrames += frames;
        });
    REQUIRE(info.sampleRate == 48000);
    REQUIRE(info.channels == 1);
    REQUIRE(info.frameCount == 4096);
    REQUIRE(streamedFrames == info.frameCount);
}
