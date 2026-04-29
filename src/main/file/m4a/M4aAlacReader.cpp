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
        class BufferedPacketReader
        {
        public:
            BufferedPacketReader(std::ifstream &inputToUse,
                                 const std::filesystem::path &pathToRead,
                                 const std::uint64_t totalBytesToRead,
                                 M4aReadProgressCallback progressCallbackToUse)
                : input(inputToUse),
                  path(pathToRead),
                  totalBytes(totalBytesToRead),
                  progressCallback(std::move(progressCallbackToUse))
            {
            }

            bool readPacket(const std::uint64_t packetOffset,
                            const std::uint32_t packetSize,
                            cupuacu::file::alac::PacketBufferView &packetView)
            {
                if (packetOffset > totalBytes ||
                    packetSize > totalBytes - packetOffset)
                {
                    return false;
                }

                ensureBufferCovers(packetOffset, packetSize);
                const auto offsetInBuffer = static_cast<std::size_t>(
                    packetOffset - bufferOffset);
                packetView.bytes =
                    buffer.data() + static_cast<std::ptrdiff_t>(offsetInBuffer);
                packetView.accessibleByteCount = buffer.size() - offsetInBuffer;
                return true;
            }

        private:
            void ensureBufferCovers(const std::uint64_t packetOffset,
                                    const std::uint32_t packetSize)
            {
                if (packetOffset >= bufferOffset)
                {
                    const auto offsetInBuffer = packetOffset - bufferOffset;
                    if (offsetInBuffer <= buffer.size() &&
                        packetSize <= buffer.size() - offsetInBuffer)
                    {
                        return;
                    }
                }

                constexpr std::uint64_t kReadChunkBytes = 4u * 1024u * 1024u;
                const auto readOffset = packetOffset;
                const auto requiredSize = std::min<std::uint64_t>(
                    totalBytes - readOffset,
                    static_cast<std::uint64_t>(packetSize) + 3u);
                const auto readSize = std::min<std::uint64_t>(
                    totalBytes - readOffset,
                    std::max<std::uint64_t>(requiredSize, kReadChunkBytes));
                buffer.resize(static_cast<std::size_t>(readSize));

                input.clear();
                input.seekg(static_cast<std::streamoff>(readOffset),
                            std::ios::beg);
                input.read(reinterpret_cast<char *>(buffer.data()),
                           static_cast<std::streamsize>(readSize));
                if (input.gcount() != static_cast<std::streamsize>(readSize))
                {
                    throw std::runtime_error("Failed to read M4A packet: " +
                                             path.string());
                }

                bufferOffset = readOffset;
                if (progressCallback)
                {
                    progressCallback(
                        std::min<std::uint64_t>(totalBytes,
                                                readOffset + readSize),
                        totalBytes);
                }
            }

            std::ifstream &input;
            const std::filesystem::path &path;
            std::uint64_t totalBytes = 0;
            M4aReadProgressCallback progressCallback;
            std::vector<std::uint8_t> buffer;
            std::uint64_t bufferOffset = 0;
        };

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

    M4aAlacFileInfo streamAlacM4aFile(
        const std::filesystem::path &path,
        M4aDecodedPcmBlockCallback decodedPcmBlockCallback,
        M4aAlacFileInfoCallback fileInfoCallback,
        cupuacu::file::alac::DecodeProgressCallback progressCallback,
        M4aReadProgressCallback readProgressCallback)
    {
        if (!decodedPcmBlockCallback)
        {
            throw std::runtime_error("Missing M4A ALAC decode sink");
        }

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
        if (readProgressCallback)
        {
            readProgressCallback(0, totalBytes);
        }

        input.clear();
        input.seekg(0, std::ios::beg);
        const auto parsed = parseAlacM4aFile(input);
        if (parsed.bitDepth != 16 && parsed.bitDepth != 24 &&
            parsed.bitDepth != 32)
        {
            throw std::runtime_error(
                "Only 16, 24, and 32-bit ALAC M4A files are currently supported");
        }

        const M4aAlacFileInfo fileInfo{
            .sampleRate = parsed.sampleRate,
            .channels = parsed.channels,
            .bitDepth = parsed.bitDepth,
            .frameCount = parsed.frameCount,
            .sampleFormat = sampleFormatForBitDepth(parsed.bitDepth),
            .markers = parsed.markers,
        };
        if (fileInfoCallback)
        {
            fileInfoCallback(fileInfo);
        }

        BufferedPacketReader packetReader(input, path, totalBytes,
                                          readProgressCallback);
        const auto ok = cupuacu::file::alac::streamDecodedPcmPackets(
            {
                .sampleRate = parsed.sampleRate,
                .channels = parsed.channels,
                .bitsPerSample = parsed.bitDepth,
                .framesPerPacket = parsed.framesPerPacket,
                .frameCount = parsed.frameCount,
                .magicCookie = parsed.magicCookie,
                .packetFrameCounts = parsed.packetFrameCounts,
            },
            parsed.packetOffsets, parsed.packetSizes,
            [&packetReader](const std::uint64_t packetOffset,
                            const std::uint32_t packetSize,
                            cupuacu::file::alac::PacketBufferView &packetView)
            { return packetReader.readPacket(packetOffset, packetSize,
                                             packetView); },
            [&](const std::uint8_t *interleavedPcmBytes,
                const std::uint32_t pcmByteCount,
                const std::uint32_t frameCount)
            {
                decodedPcmBlockCallback(interleavedPcmBytes, pcmByteCount,
                                        frameCount, parsed.channels,
                                        parsed.bitDepth);
                return true;
            },
            std::move(progressCallback));
        if (!ok)
        {
            throw std::runtime_error("Failed to decode ALAC M4A packets");
        }
        if (readProgressCallback)
        {
            readProgressCallback(totalBytes, totalBytes);
        }

        return fileInfo;
    }

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
            bytes, parsed.packetOffsets, parsed.packetSizes,
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
        M4aAlacPcmData audio;
        const auto info = streamAlacM4aFile(
            path,
            [&audio](const std::uint8_t *interleavedPcmBytes,
                     const std::uint32_t pcmByteCount,
                     const std::uint32_t,
                     const std::uint16_t,
                     const std::uint16_t)
            {
                audio.interleavedPcmBytes.insert(
                    audio.interleavedPcmBytes.end(), interleavedPcmBytes,
                    interleavedPcmBytes +
                        static_cast<std::ptrdiff_t>(pcmByteCount));
            },
            {}, std::move(progressCallback), std::move(readProgressCallback));
        audio.sampleRate = info.sampleRate;
        audio.channels = info.channels;
        audio.bitDepth = info.bitDepth;
        audio.frameCount = info.frameCount;
        audio.sampleFormat = info.sampleFormat;
        audio.markers = std::move(info.markers);
        return audio;
    }
} // namespace cupuacu::file::m4a
