#include <catch2/catch_test_macros.hpp>

#include "file/alac/AlacCodec.hpp"
#include "file/m4a/M4aAtoms.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    std::string asciiAt(const cupuacu::file::m4a::Bytes &bytes,
                        const std::size_t offset)
    {
        return std::string(reinterpret_cast<const char *>(bytes.data() + offset),
                           4);
    }

    std::uint32_t readBe32(const cupuacu::file::m4a::Bytes &bytes,
                           const std::size_t offset)
    {
        return (static_cast<std::uint32_t>(bytes[offset]) << 24u) |
               (static_cast<std::uint32_t>(bytes[offset + 1]) << 16u) |
               (static_cast<std::uint32_t>(bytes[offset + 2]) << 8u) |
               static_cast<std::uint32_t>(bytes[offset + 3]);
    }

    std::size_t findChildOffset(const cupuacu::file::m4a::Bytes &bytes,
                                const std::size_t childrenStart,
                                const std::string &type)
    {
        for (std::size_t offset = childrenStart; offset + 8 <= bytes.size();)
        {
            const auto size = readBe32(bytes, offset);
            if (size < 8 || offset + size > bytes.size())
            {
                throw std::runtime_error("Invalid child atom size");
            }
            if (asciiAt(bytes, offset + 4) == type)
            {
                return offset;
            }
            offset += size;
        }
        throw std::runtime_error("Child atom not found");
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
} // namespace

TEST_CASE("M4A atom writer serializes big-endian primitives", "[m4a]")
{
    cupuacu::file::m4a::Bytes bytes;
    cupuacu::file::m4a::appendBe16(bytes, 0x1234u);
    cupuacu::file::m4a::appendBe24(bytes, 0xabcdefu);
    cupuacu::file::m4a::appendBe32(bytes, 0x01020304u);
    cupuacu::file::m4a::appendBe64(bytes, 0x0102030405060708ull);

    REQUIRE(bytes == cupuacu::file::m4a::Bytes{
                         0x12, 0x34, 0xab, 0xcd, 0xef, 0x01, 0x02,
                         0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x05,
                         0x06, 0x07, 0x08});
}

TEST_CASE("M4A atom writer validates FourCC and integer bounds", "[m4a]")
{
    cupuacu::file::m4a::Bytes bytes;

    REQUIRE_THROWS_AS(cupuacu::file::m4a::appendFourCc(bytes, "abc"),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(cupuacu::file::m4a::appendBe24(bytes, 0x01000000u),
                      std::out_of_range);
}

TEST_CASE("M4A ftyp atom declares M4A compatible brands", "[m4a]")
{
    const auto ftyp = cupuacu::file::m4a::ftypAtom();

    REQUIRE(readBe32(ftyp, 0) == 28);
    REQUIRE(asciiAt(ftyp, 4) == "ftyp");
    REQUIRE(asciiAt(ftyp, 8) == "M4A ");
    REQUIRE(readBe32(ftyp, 12) == 0);
    REQUIRE(asciiAt(ftyp, 16) == "M4A ");
    REQUIRE(asciiAt(ftyp, 20) == "mp42");
    REQUIRE(asciiAt(ftyp, 24) == "isom");
}

TEST_CASE("M4A empty sample table atoms carry zero entries", "[m4a]")
{
    const auto stts = cupuacu::file::m4a::emptyTimeToSampleAtom();
    const auto stsc = cupuacu::file::m4a::emptySampleToChunkAtom();
    const auto stsz = cupuacu::file::m4a::emptySampleSizeAtom();
    const auto stco = cupuacu::file::m4a::emptyChunkOffsetAtom();

    REQUIRE(readBe32(stts, 0) == 16);
    REQUIRE(asciiAt(stts, 4) == "stts");
    REQUIRE(readBe32(stts, 12) == 0);

    REQUIRE(readBe32(stsc, 0) == 16);
    REQUIRE(asciiAt(stsc, 4) == "stsc");
    REQUIRE(readBe32(stsc, 12) == 0);

    REQUIRE(readBe32(stsz, 0) == 20);
    REQUIRE(asciiAt(stsz, 4) == "stsz");
    REQUIRE(readBe32(stsz, 12) == 0);
    REQUIRE(readBe32(stsz, 16) == 0);

    REQUIRE(readBe32(stco, 0) == 16);
    REQUIRE(asciiAt(stco, 4) == "stco");
    REQUIRE(readBe32(stco, 12) == 0);
}

TEST_CASE("M4A time-to-sample atom represents full and partial ALAC packets",
          "[m4a]")
{
    const auto stts = cupuacu::file::m4a::timeToSampleAtom(5, 2);

    REQUIRE(readBe32(stts, 0) == 32);
    REQUIRE(asciiAt(stts, 4) == "stts");
    REQUIRE(readBe32(stts, 12) == 2);
    REQUIRE(readBe32(stts, 16) == 2);
    REQUIRE(readBe32(stts, 20) == 2);
    REQUIRE(readBe32(stts, 24) == 1);
    REQUIRE(readBe32(stts, 28) == 1);
}

TEST_CASE("M4A time-to-sample atom represents uniform ALAC packets", "[m4a]")
{
    const auto stts = cupuacu::file::m4a::timeToSampleAtom(6, 3);

    REQUIRE(readBe32(stts, 0) == 24);
    REQUIRE(asciiAt(stts, 4) == "stts");
    REQUIRE(readBe32(stts, 12) == 1);
    REQUIRE(readBe32(stts, 16) == 2);
    REQUIRE(readBe32(stts, 20) == 3);
}

TEST_CASE("M4A time-to-sample atom represents one partial ALAC packet", "[m4a]")
{
    const auto stts = cupuacu::file::m4a::timeToSampleAtom(2, 5);

    REQUIRE(readBe32(stts, 0) == 24);
    REQUIRE(asciiAt(stts, 4) == "stts");
    REQUIRE(readBe32(stts, 12) == 1);
    REQUIRE(readBe32(stts, 16) == 1);
    REQUIRE(readBe32(stts, 20) == 2);
}

TEST_CASE("M4A populated sample table builders return empty atoms for no packets",
          "[m4a]")
{
    REQUIRE(cupuacu::file::m4a::timeToSampleAtom(0, 4096) ==
            cupuacu::file::m4a::emptyTimeToSampleAtom());
    REQUIRE(cupuacu::file::m4a::sampleToChunkAtom(0) ==
            cupuacu::file::m4a::emptySampleToChunkAtom());
    REQUIRE(cupuacu::file::m4a::sampleSizeAtom({}) ==
            cupuacu::file::m4a::emptySampleSizeAtom());
    REQUIRE(cupuacu::file::m4a::chunkOffsetAtom({}) ==
            cupuacu::file::m4a::emptyChunkOffsetAtom());
}

TEST_CASE("M4A time-to-sample atom rejects zero packet duration", "[m4a]")
{
    REQUIRE_THROWS_AS(cupuacu::file::m4a::timeToSampleAtom(1, 0),
                      std::invalid_argument);
}

TEST_CASE("M4A sample-to-chunk atom stores all packets in one chunk", "[m4a]")
{
    const auto stsc = cupuacu::file::m4a::sampleToChunkAtom(3);

    REQUIRE(readBe32(stsc, 0) == 28);
    REQUIRE(asciiAt(stsc, 4) == "stsc");
    REQUIRE(readBe32(stsc, 12) == 1);
    REQUIRE(readBe32(stsc, 16) == 1);
    REQUIRE(readBe32(stsc, 20) == 3);
    REQUIRE(readBe32(stsc, 24) == 1);
}

TEST_CASE("M4A sample-size atom lists encoded packet sizes", "[m4a]")
{
    const auto stsz = cupuacu::file::m4a::sampleSizeAtom({10, 20, 30});

    REQUIRE(readBe32(stsz, 0) == 32);
    REQUIRE(asciiAt(stsz, 4) == "stsz");
    REQUIRE(readBe32(stsz, 12) == 0);
    REQUIRE(readBe32(stsz, 16) == 3);
    REQUIRE(readBe32(stsz, 20) == 10);
    REQUIRE(readBe32(stsz, 24) == 20);
    REQUIRE(readBe32(stsz, 28) == 30);
}

TEST_CASE("M4A chunk-offset atom lists mdat payload offsets", "[m4a]")
{
    const auto stco = cupuacu::file::m4a::chunkOffsetAtom({128});

    REQUIRE(readBe32(stco, 0) == 20);
    REQUIRE(asciiAt(stco, 4) == "stco");
    REQUIRE(readBe32(stco, 12) == 1);
    REQUIRE(readBe32(stco, 16) == 128);
}

TEST_CASE("M4A ALAC sample entry embeds the ALAC cookie", "[m4a]")
{
    const cupuacu::file::m4a::Bytes cookie{0x11, 0x22, 0x33, 0x44};
    const auto entry = cupuacu::file::m4a::alacSampleEntry({
        .channels = 2,
        .bitDepth = 16,
        .sampleRate = 44100,
        .magicCookie = cookie,
    });

    REQUIRE(readBe32(entry, 0) == 52);
    REQUIRE(asciiAt(entry, 4) == "alac");
    REQUIRE(readBe32(entry, 36) == 16);
    REQUIRE(asciiAt(entry, 40) == "alac");
    REQUIRE(readBe32(entry, 44) == 0);
    REQUIRE(entry[48] == 0x11);
    REQUIRE(entry[49] == 0x22);
    REQUIRE(entry[50] == 0x33);
    REQUIRE(entry[51] == 0x44);
}

TEST_CASE("M4A ALAC sample entry rejects invalid descriptions", "[m4a]")
{
    REQUIRE_THROWS_AS(cupuacu::file::m4a::alacSampleEntry({
                          .channels = 2,
                          .bitDepth = 16,
                          .sampleRate = 44100,
                      }),
                      std::invalid_argument);
}

TEST_CASE("M4A ALAC sample entry preserves high sample rates for the cookie",
          "[m4a]")
{
    const auto entry = cupuacu::file::m4a::alacSampleEntry({
        .channels = 2,
        .bitDepth = 24,
        .sampleRate = 96000,
        .magicCookie = {0x01},
    });

    REQUIRE(readBe32(entry, 32) == 0);
}

TEST_CASE("M4A sample description wraps sample entries", "[m4a]")
{
    const auto entry = cupuacu::file::m4a::alacSampleEntry({
        .channels = 1,
        .bitDepth = 24,
        .sampleRate = 48000,
        .magicCookie = {0x01, 0x02},
    });
    const auto stsd = cupuacu::file::m4a::sampleDescriptionAtom({entry});

    REQUIRE(readBe32(stsd, 0) == 16 + entry.size());
    REQUIRE(asciiAt(stsd, 4) == "stsd");
    REQUIRE(readBe32(stsd, 12) == 1);
    REQUIRE(readBe32(stsd, 16) == entry.size());
    REQUIRE(asciiAt(stsd, 20) == "alac");
}

TEST_CASE("M4A movie and track headers expose audio timing", "[m4a]")
{
    const auto mvhd = cupuacu::file::m4a::movieHeaderAtom(44100, 1000);
    const auto tkhd = cupuacu::file::m4a::trackHeaderAtom(1, 1000);
    const auto mdhd = cupuacu::file::m4a::mediaHeaderAtom(44100, 1000);

    REQUIRE(asciiAt(mvhd, 4) == "mvhd");
    REQUIRE(readBe32(mvhd, 20) == 44100);
    REQUIRE(readBe32(mvhd, 24) == 1000);
    REQUIRE(readBe32(mvhd, 104) == 2);

    REQUIRE(asciiAt(tkhd, 4) == "tkhd");
    REQUIRE(readBe32(tkhd, 20) == 1);
    REQUIRE(readBe32(tkhd, 28) == 1000);

    REQUIRE(asciiAt(mdhd, 4) == "mdhd");
    REQUIRE(readBe32(mdhd, 20) == 44100);
    REQUIRE(readBe32(mdhd, 24) == 1000);
}

TEST_CASE("M4A audio media support atoms declare sound and self-contained data",
          "[m4a]")
{
    const auto hdlr = cupuacu::file::m4a::handlerReferenceAtom();
    const auto smhd = cupuacu::file::m4a::soundMediaHeaderAtom();
    const auto dinf = cupuacu::file::m4a::dataInformationAtom();

    REQUIRE(asciiAt(hdlr, 4) == "hdlr");
    REQUIRE(asciiAt(hdlr, 16) == "soun");

    REQUIRE(readBe32(smhd, 0) == 16);
    REQUIRE(asciiAt(smhd, 4) == "smhd");

    REQUIRE(asciiAt(dinf, 4) == "dinf");
    REQUIRE(asciiAt(dinf, 12) == "dref");
    REQUIRE(readBe32(dinf, 20) == 1);
    REQUIRE(asciiAt(dinf, 28) == "url ");
    REQUIRE(readBe32(dinf, 32) == 1);
}

TEST_CASE("M4A sample table composes ALAC metadata and packet tables", "[m4a]")
{
    const cupuacu::file::m4a::AlacMovieDescription description{
        .sampleRate = 44100,
        .frameCount = 5,
        .framesPerPacket = 2,
        .packetSizes = {10, 20, 30},
        .mdatPayloadOffset = 128,
        .sampleEntry =
            {
                .channels = 2,
                .bitDepth = 16,
                .sampleRate = 44100,
                .magicCookie = {0x01, 0x02},
            },
    };

    const auto stbl = cupuacu::file::m4a::sampleTableAtom(description);
    const auto stsdOffset = findChildOffset(stbl, 8, "stsd");
    const auto sttsOffset = findChildOffset(stbl, 8, "stts");
    const auto stscOffset = findChildOffset(stbl, 8, "stsc");
    const auto stszOffset = findChildOffset(stbl, 8, "stsz");
    const auto stcoOffset = findChildOffset(stbl, 8, "stco");

    REQUIRE(asciiAt(stbl, 4) == "stbl");
    REQUIRE(asciiAt(stbl, stsdOffset + 4) == "stsd");
    REQUIRE(asciiAt(stbl, sttsOffset + 4) == "stts");
    REQUIRE(asciiAt(stbl, stscOffset + 4) == "stsc");
    REQUIRE(asciiAt(stbl, stszOffset + 4) == "stsz");
    REQUIRE(asciiAt(stbl, stcoOffset + 4) == "stco");
    REQUIRE(readBe32(stbl, stcoOffset + 12) == 1);
    REQUIRE(readBe32(stbl, stcoOffset + 16) == 128);
}

TEST_CASE("M4A movie atom contains mvhd and one audio track", "[m4a]")
{
    const cupuacu::file::m4a::AlacMovieDescription description{
        .sampleRate = 48000,
        .frameCount = 4,
        .framesPerPacket = 2,
        .packetSizes = {12, 14},
        .mdatPayloadOffset = 256,
        .sampleEntry =
            {
                .channels = 1,
                .bitDepth = 24,
                .sampleRate = 48000,
                .magicCookie = {0xaa, 0xbb},
            },
    };

    const auto moov = cupuacu::file::m4a::movieAtom(description);
    const auto mvhdOffset = findChildOffset(moov, 8, "mvhd");
    const auto trakOffset = findChildOffset(moov, 8, "trak");
    const auto mdiaOffset = findChildOffset(moov, trakOffset + 8, "mdia");

    REQUIRE(asciiAt(moov, 4) == "moov");
    REQUIRE(asciiAt(moov, mvhdOffset + 4) == "mvhd");
    REQUIRE(asciiAt(moov, trakOffset + 4) == "trak");
    REQUIRE(asciiAt(moov, mdiaOffset + 4) == "mdia");
}

TEST_CASE("M4A movie atom rejects invalid timing metadata", "[m4a]")
{
    REQUIRE_THROWS_AS(cupuacu::file::m4a::movieAtom({
                          .frameCount = 1,
                          .framesPerPacket = 1,
                          .packetSizes = {1},
                          .mdatPayloadOffset = 128,
                          .sampleEntry =
                              {
                                  .channels = 1,
                                  .bitDepth = 16,
                                  .sampleRate = 44100,
                                  .magicCookie = {0x01},
                              },
                      }),
                      std::invalid_argument);
}

TEST_CASE("M4A assembler writes ftyp mdat moov with stable media offset",
          "[m4a]")
{
    const auto encoded = cupuacu::file::alac::encodePcmPackets(
        {
            .sampleRate = 44100,
            .channels = 2,
            .bitsPerSample = 16,
            .framesPerPacket = cupuacu::file::alac::defaultFramesPerPacket(),
        },
        makeStereoPcm16(cupuacu::file::alac::defaultFramesPerPacket()));
    REQUIRE(encoded.has_value());

    const auto file = cupuacu::file::m4a::assembleAlacM4a(*encoded);
    const auto ftypSize = readBe32(file, 0);
    const auto mdatOffset = static_cast<std::size_t>(ftypSize);
    const auto mdatSize = readBe32(file, mdatOffset);
    const auto moovOffset = mdatOffset + mdatSize;

    REQUIRE(asciiAt(file, 4) == "ftyp");
    REQUIRE(asciiAt(file, mdatOffset + 4) == "mdat");
    REQUIRE(asciiAt(file, moovOffset + 4) == "moov");
    REQUIRE(std::vector<std::uint8_t>(file.begin() + mdatOffset + 8,
                                      file.begin() + moovOffset) ==
            encoded->bytes);

    const auto mvhdOffset = findChildOffset(file, moovOffset + 8, "mvhd");
    const auto trakOffset = findChildOffset(file, moovOffset + 8, "trak");
    const auto mdiaOffset = findChildOffset(file, trakOffset + 8, "mdia");
    const auto minfOffset = findChildOffset(file, mdiaOffset + 8, "minf");
    const auto stblOffset = findChildOffset(file, minfOffset + 8, "stbl");
    const auto stcoOffset = findChildOffset(file, stblOffset + 8, "stco");

    REQUIRE(readBe32(file, mvhdOffset + 20) == 44100);
    REQUIRE(readBe32(file, stcoOffset + 12) == 1);
    REQUIRE(readBe32(file, stcoOffset + 16) == ftypSize + 8);
}

TEST_CASE("M4A assembler rejects empty ALAC packet metadata", "[m4a]")
{
    REQUIRE_THROWS_AS(cupuacu::file::m4a::assembleAlacM4a({}),
                      std::invalid_argument);
}
