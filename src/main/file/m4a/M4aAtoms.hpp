#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "../alac/AlacCodec.hpp"

namespace cupuacu::file::m4a
{
    using Bytes = std::vector<std::uint8_t>;

    struct AlacSampleEntryDescription
    {
        std::uint16_t channels = 0;
        std::uint16_t bitDepth = 0;
        std::uint32_t sampleRate = 0;
        Bytes magicCookie;
    };

    struct AlacMovieDescription
    {
        std::uint32_t sampleRate = 0;
        std::uint32_t frameCount = 0;
        std::uint32_t framesPerPacket = 0;
        std::vector<std::uint32_t> packetSizes;
        std::uint32_t mdatPayloadOffset = 0;
        AlacSampleEntryDescription sampleEntry;
    };

    void appendU8(Bytes &bytes, std::uint8_t value);
    void appendBe16(Bytes &bytes, std::uint16_t value);
    void appendBe24(Bytes &bytes, std::uint32_t value);
    void appendBe32(Bytes &bytes, std::uint32_t value);
    void appendBe64(Bytes &bytes, std::uint64_t value);
    void appendFourCc(Bytes &bytes, std::string_view fourCc);
    Bytes atom(std::string_view type, const Bytes &payload = {});
    Bytes fullAtom(std::string_view type,
                   std::uint8_t version,
                   std::uint32_t flags,
                   const Bytes &payload = {});
    Bytes containerAtom(std::string_view type,
                        const std::vector<Bytes> &children);

    Bytes ftypAtom();
    Bytes mdatAtom(const Bytes &payload);
    Bytes emptyTimeToSampleAtom();
    Bytes emptySampleToChunkAtom();
    Bytes emptySampleSizeAtom();
    Bytes emptyChunkOffsetAtom();
    Bytes timeToSampleAtom(std::uint32_t frameCount,
                           std::uint32_t framesPerPacket);
    Bytes sampleToChunkAtom(std::uint32_t packetCount);
    Bytes sampleSizeAtom(const std::vector<std::uint32_t> &packetSizes);
    Bytes chunkOffsetAtom(const std::vector<std::uint32_t> &chunkOffsets);
    Bytes alacSampleEntry(const AlacSampleEntryDescription &description);
    Bytes sampleDescriptionAtom(const std::vector<Bytes> &sampleEntries);
    Bytes movieHeaderAtom(std::uint32_t timescale, std::uint32_t duration);
    Bytes trackHeaderAtom(std::uint32_t trackId, std::uint32_t duration);
    Bytes mediaHeaderAtom(std::uint32_t timescale, std::uint32_t duration);
    Bytes soundMediaHeaderAtom();
    Bytes handlerReferenceAtom();
    Bytes dataInformationAtom();
    Bytes sampleTableAtom(const AlacMovieDescription &description);
    Bytes mediaInformationAtom(const AlacMovieDescription &description);
    Bytes mediaAtom(const AlacMovieDescription &description);
    Bytes trackAtom(const AlacMovieDescription &description);
    Bytes movieAtom(const AlacMovieDescription &description);
    Bytes assembleAlacM4a(const cupuacu::file::alac::AlacEncodedPackets &packets);
} // namespace cupuacu::file::m4a
