#pragma once

#include "M4aAtoms.hpp"

#include "../../Document.hpp"

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <vector>

namespace cupuacu::file::m4a
{
    enum class M4aAudioCodec
    {
        ALAC,
        AAC_LC,
    };

    struct M4aParsedAlacFile
    {
        std::uint32_t sampleRate = 0;
        std::uint16_t channels = 0;
        std::uint16_t bitDepth = 0;
        std::uint32_t frameCount = 0;
        std::uint32_t framesPerPacket = 0;
        std::uint64_t mdatPayloadOffset = 0;
        std::uint64_t mdatPayloadSize = 0;
        std::vector<std::uint32_t> packetSizes;
        std::vector<std::uint32_t> packetFrameCounts;
        std::vector<std::uint64_t> packetOffsets;
        Bytes magicCookie;
        std::vector<cupuacu::DocumentMarker> markers;
    };

    struct M4aParsedAacFile
    {
        std::uint32_t sampleRate = 0;
        std::uint16_t channels = 0;
        std::uint32_t frameCount = 0;
        std::uint32_t framesPerPacket = 0;
        std::uint32_t primingFrames = 0;
        std::uint32_t paddingFrames = 0;
        std::uint64_t mdatPayloadOffset = 0;
        std::uint64_t mdatPayloadSize = 0;
        std::vector<std::uint32_t> packetSizes;
        std::vector<std::uint32_t> packetFrameCounts;
        std::vector<std::uint64_t> packetOffsets;
        Bytes audioSpecificConfig;
        std::vector<cupuacu::DocumentMarker> markers;
    };

    [[nodiscard]] M4aAudioCodec detectM4aAudioCodec(const Bytes &bytes);
    [[nodiscard]] M4aAudioCodec
    detectM4aAudioCodecFile(const std::filesystem::path &path);
    [[nodiscard]] M4aParsedAacFile parseAacM4a(const Bytes &bytes);
    [[nodiscard]] M4aParsedAacFile
    parseAacM4aFile(const std::filesystem::path &path);
    [[nodiscard]] M4aParsedAacFile parseAacM4aFile(std::ifstream &input);

    [[nodiscard]] M4aParsedAlacFile parseAlacM4a(const Bytes &bytes);
    [[nodiscard]] M4aParsedAlacFile
    parseAlacM4aFile(const std::filesystem::path &path);
    [[nodiscard]] M4aParsedAlacFile parseAlacM4aFile(std::ifstream &input);
} // namespace cupuacu::file::m4a
