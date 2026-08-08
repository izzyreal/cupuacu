#include "M4aAacReader.hpp"

#include "M4aParser.hpp"
#include "../aac/AacCodec.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace cupuacu::file::m4a
{
    M4aAacFileInfo
    streamAacM4aFile(const std::filesystem::path &path,
                     M4aDecodedFloatBlockCallback decodedBlockCallback,
                     M4aAacFileInfoCallback fileInfoCallback,
                     M4aAacDecodeProgressCallback progressCallback,
                     M4aReadProgressCallback readProgressCallback)
    {
        if (!decodedBlockCallback)
        {
            throw std::runtime_error("Missing M4A AAC decode sink");
        }
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
        {
            throw std::runtime_error("Failed to open M4A file: " +
                                     path.string());
        }
        const auto endPosition = input.tellg();
        if (endPosition < 0)
        {
            throw std::runtime_error("Failed to measure M4A file: " +
                                     path.string());
        }
        const auto totalBytes = static_cast<std::uint64_t>(endPosition);
        if (readProgressCallback)
        {
            readProgressCallback(0, totalBytes);
        }

        input.clear();
        input.seekg(0, std::ios::beg);
        const auto parsed = parseAacM4aFile(input);
        cupuacu::file::aac::Decoder decoder(parsed.audioSpecificConfig);
        if (decoder.sampleRate() != parsed.sampleRate ||
            decoder.channels() != parsed.channels)
        {
            throw std::runtime_error(
                "AAC decoder configuration does not match M4A metadata");
        }
        const auto outputFrameCount =
            parsed.frameCount - parsed.primingFrames - parsed.paddingFrames;
        const M4aAacFileInfo fileInfo{
            .sampleRate = parsed.sampleRate,
            .channels = parsed.channels,
            .frameCount = outputFrameCount,
            .sampleFormat = cupuacu::SampleFormat::FLOAT32,
            .markers = parsed.markers,
        };
        if (fileInfoCallback)
        {
            fileInfoCallback(fileInfo);
        }

        std::uint64_t decodedFrameOffset = 0;
        std::uint64_t deliveredFrames = 0;
        const auto audibleBegin =
            static_cast<std::uint64_t>(parsed.primingFrames);
        const auto audibleEnd = static_cast<std::uint64_t>(
            parsed.frameCount - parsed.paddingFrames);
        std::vector<std::uint8_t> packet;
        for (std::size_t i = 0; i < parsed.packetSizes.size(); ++i)
        {
            packet.resize(parsed.packetSizes[i]);
            input.clear();
            input.seekg(static_cast<std::streamoff>(parsed.packetOffsets[i]),
                        std::ios::beg);
            input.read(reinterpret_cast<char *>(packet.data()),
                       static_cast<std::streamsize>(packet.size()));
            if (input.gcount() != static_cast<std::streamsize>(packet.size()))
            {
                throw std::runtime_error("Failed to read AAC M4A packet: " +
                                         path.string());
            }

            const auto decoded = decoder.decode(packet);
            const auto blockBegin = decodedFrameOffset;
            const auto blockEnd = blockBegin + decoded.frameCount;
            const auto keepBegin = std::max(blockBegin, audibleBegin);
            const auto keepEnd = std::min(blockEnd, audibleEnd);
            if (keepBegin < keepEnd)
            {
                const auto skipFrames = keepBegin - blockBegin;
                const auto keepFrames = keepEnd - keepBegin;
                decodedBlockCallback(decoded.interleavedSamples.data() +
                                         static_cast<std::ptrdiff_t>(
                                             skipFrames * parsed.channels),
                                     static_cast<std::uint32_t>(keepFrames),
                                     parsed.channels);
                deliveredFrames += keepFrames;
            }
            decodedFrameOffset = blockEnd;
            if (progressCallback)
            {
                progressCallback(
                    static_cast<std::uint32_t>(std::min<std::uint64_t>(
                        deliveredFrames, outputFrameCount)),
                    outputFrameCount);
            }
            if (readProgressCallback)
            {
                readProgressCallback(
                    std::min<std::uint64_t>(
                        totalBytes, parsed.packetOffsets[i] + packet.size()),
                    totalBytes);
            }
        }
        if (decodedFrameOffset != parsed.frameCount ||
            deliveredFrames != outputFrameCount)
        {
            throw std::runtime_error("Decoded AAC M4A sample count mismatch");
        }
        if (readProgressCallback)
        {
            readProgressCallback(totalBytes, totalBytes);
        }
        return fileInfo;
    }
} // namespace cupuacu::file::m4a
