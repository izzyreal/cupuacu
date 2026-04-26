#include "M4aAlacReader.hpp"

#include "M4aParser.hpp"
#include "../alac/AlacCodec.hpp"

#include <cstddef>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <stdexcept>
#include <utility>

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

        cupuacu::SampleFormat sampleFormatForBitDepth(
            const std::uint16_t bitDepth)
        {
            switch (bitDepth)
            {
                case 16:
                    return cupuacu::SampleFormat::PCM_S16;
                case 24:
                    return cupuacu::SampleFormat::PCM_S24;
                case 32:
                    return cupuacu::SampleFormat::PCM_S32;
                default:
                    throw std::runtime_error(
                        "Unsupported ALAC bit depth in M4A file");
            }
        }
    } // namespace

    M4aAlacPcmData readAlacM4a(
        const Bytes &bytes,
        cupuacu::file::alac::DecodeProgressCallback progressCallback)
    {
        const auto parsed = parseAlacM4a(bytes);
        if (parsed.bitDepth != 16 && parsed.bitDepth != 24 &&
            parsed.bitDepth != 32)
        {
            throw std::runtime_error(
                "Only 16, 24, and 32-bit ALAC M4A files are currently supported");
        }

        const auto decoded = cupuacu::file::alac::decodePcmPackets(
            {
                .sampleRate = parsed.sampleRate,
                .channels = parsed.channels,
                .bitsPerSample = parsed.bitDepth,
                .framesPerPacket = parsed.framesPerPacket,
                .frameCount = parsed.frameCount,
                .magicCookie = parsed.magicCookie,
                .packetFrameCounts = parsed.packetFrameCounts,
            },
            collectPacketBytes(bytes, parsed), parsed.packetSizes,
            std::move(progressCallback));
        if (!decoded.has_value())
        {
            throw std::runtime_error("Failed to decode ALAC M4A packets");
        }
        const auto bytesPerSample =
            static_cast<std::size_t>((decoded->bitsPerSample + 7u) / 8u);
        if (decoded->interleavedPcmBytes.size() !=
            static_cast<std::size_t>(decoded->frameCount) * decoded->channels *
                bytesPerSample)
        {
            throw std::runtime_error("Decoded ALAC M4A sample count mismatch");
        }

        return {
            .sampleRate = decoded->sampleRate,
            .channels = static_cast<std::uint16_t>(decoded->channels),
            .bitDepth = static_cast<std::uint16_t>(decoded->bitsPerSample),
            .frameCount = decoded->frameCount,
            .sampleFormat = sampleFormatForBitDepth(
                static_cast<std::uint16_t>(decoded->bitsPerSample)),
            .interleavedPcmBytes = decoded->interleavedPcmBytes,
            .markers = parsed.markers,
        };
    }

    M4aAlacPcmData readAlacM4aFile(
        const std::filesystem::path &path,
        cupuacu::file::alac::DecodeProgressCallback progressCallback,
        M4aReadProgressCallback readProgressCallback)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
        {
            throw std::runtime_error("Failed to open M4A file: " + path.string());
        }

        const auto endPosition = input.tellg();
        if (endPosition < 0)
        {
            throw std::runtime_error("Failed to measure M4A file: " +
                                     path.string());
        }
        const auto totalBytes = static_cast<std::uint64_t>(endPosition);
        input.seekg(0, std::ios::beg);

        Bytes bytes;
        bytes.resize(static_cast<std::size_t>(totalBytes));
        constexpr std::size_t kReadBlockBytes = 1024u * 1024u;
        std::uint64_t bytesRead = 0;
        if (readProgressCallback)
        {
            readProgressCallback(0, totalBytes);
        }
        while (bytesRead < totalBytes)
        {
            const auto bytesToRead = std::min<std::uint64_t>(
                kReadBlockBytes, totalBytes - bytesRead);
            input.read(
                reinterpret_cast<char *>(bytes.data() +
                                          static_cast<std::size_t>(bytesRead)),
                static_cast<std::streamsize>(bytesToRead));
            if (input.gcount() <= 0)
            {
                throw std::runtime_error("Failed to read M4A file: " +
                                         path.string());
            }
            bytesRead += static_cast<std::uint64_t>(input.gcount());
            if (readProgressCallback)
            {
                readProgressCallback(bytesRead, totalBytes);
            }
        }

        return readAlacM4a(bytes, std::move(progressCallback));
    }
} // namespace cupuacu::file::m4a
