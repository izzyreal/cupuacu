#include "M4aAlacReader.hpp"

#include "M4aParser.hpp"
#include "../alac/AlacCodec.hpp"

#include <cstddef>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace cupuacu::file::m4a
{
    namespace
    {
        Bytes collectPacketBytes(const Bytes &bytes,
                                 const M4aParsedAlacFile &parsed)
        {
            Bytes packetBytes;
            for (std::size_t i = 0; i < parsed.packetSizes.size(); ++i)
            {
                const auto offset = parsed.packetOffsets[i];
                const auto size = parsed.packetSizes[i];
                if (offset > bytes.size() || size > bytes.size() - offset)
                {
                    throw std::runtime_error("ALAC packet extends past file");
                }
                packetBytes.insert(
                    packetBytes.end(),
                    bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                    bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
            }
            return packetBytes;
        }
    } // namespace

    M4aAlacPcmData readAlacM4a(const Bytes &bytes)
    {
        const auto parsed = parseAlacM4a(bytes);
        if (parsed.bitDepth != 16)
        {
            throw std::runtime_error(
                "Only 16-bit ALAC M4A files are currently supported");
        }

        const auto decoded = cupuacu::file::alac::decodePcm16Packets(
            {
                .sampleRate = parsed.sampleRate,
                .channels = parsed.channels,
                .bitsPerSample = parsed.bitDepth,
                .framesPerPacket = parsed.framesPerPacket,
                .frameCount = parsed.frameCount,
                .magicCookie = parsed.magicCookie,
                .packetFrameCounts = parsed.packetFrameCounts,
            },
            collectPacketBytes(bytes, parsed), parsed.packetSizes);
        if (!decoded.has_value())
        {
            throw std::runtime_error("Failed to decode ALAC M4A packets");
        }
        if (decoded->interleavedSamples.size() !=
            static_cast<std::size_t>(decoded->frameCount) * decoded->channels)
        {
            throw std::runtime_error("Decoded ALAC M4A sample count mismatch");
        }

        return {
            .sampleRate = decoded->sampleRate,
            .channels = static_cast<std::uint16_t>(decoded->channels),
            .frameCount = decoded->frameCount,
            .interleavedPcm16Samples = decoded->interleavedSamples,
        };
    }

    M4aAlacPcmData readAlacM4aFile(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("Failed to open M4A file: " + path.string());
        }

        const Bytes bytes{std::istreambuf_iterator<char>(input),
                          std::istreambuf_iterator<char>()};
        return readAlacM4a(bytes);
    }
} // namespace cupuacu::file::m4a
