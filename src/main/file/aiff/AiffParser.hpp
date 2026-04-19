#pragma once

#include "../FileIo.hpp"
#include "AiffTypes.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace cupuacu::file::aiff
{
    namespace detail
    {
        inline std::uint16_t readBe16(const char *data)
        {
            return static_cast<std::uint16_t>(
                (static_cast<unsigned char>(data[0]) << 8) |
                static_cast<unsigned char>(data[1]));
        }

        inline std::uint32_t readBe32(const char *data)
        {
            return static_cast<std::uint32_t>(
                (static_cast<unsigned char>(data[0]) << 24) |
                (static_cast<unsigned char>(data[1]) << 16) |
                (static_cast<unsigned char>(data[2]) << 8) |
                static_cast<unsigned char>(data[3]));
        }

        inline std::ifstream openInputFileStream(const std::filesystem::path &path)
        {
            std::ifstream result(path, std::ios::binary);
            if (!result.is_open())
            {
                throw cupuacu::file::detail::makeIoFailure(
                    "Failed to open input file",
                    cupuacu::file::detail::describeErrno(errno));
            }
            return result;
        }

        inline double readExtended80(const unsigned char *bytes)
        {
            const int exponent =
                ((bytes[0] & 0x7f) << 8) | static_cast<int>(bytes[1]);
            const std::uint32_t hiMant =
                (static_cast<std::uint32_t>(bytes[2]) << 24) |
                (static_cast<std::uint32_t>(bytes[3]) << 16) |
                (static_cast<std::uint32_t>(bytes[4]) << 8) |
                static_cast<std::uint32_t>(bytes[5]);
            const std::uint32_t loMant =
                (static_cast<std::uint32_t>(bytes[6]) << 24) |
                (static_cast<std::uint32_t>(bytes[7]) << 16) |
                (static_cast<std::uint32_t>(bytes[8]) << 8) |
                static_cast<std::uint32_t>(bytes[9]);

            if (exponent == 0 && hiMant == 0 && loMant == 0)
            {
                return 0.0;
            }
            if (exponent == 0x7fff)
            {
                return std::numeric_limits<double>::infinity();
            }

            const int unbiased = exponent - 16383;
            double value = std::ldexp(static_cast<double>(hiMant), unbiased - 31);
            value += std::ldexp(static_cast<double>(loMant), unbiased - 63);
            return (bytes[0] & 0x80) != 0 ? -value : value;
        }
    } // namespace detail

    class AiffParser
    {
    public:
        [[nodiscard]] static ParsedFile parseFile(const std::filesystem::path &path)
        {
            auto input = detail::openInputFileStream(path);
            return parseStream(input, path);
        }

        [[nodiscard]] static ParsedFile parseStream(
            std::istream &input, const std::filesystem::path &path = {})
        {
            ParsedFile result{};
            result.path = path;

            input.clear();
            input.seekg(0, std::ios::beg);
            input.seekg(0, std::ios::end);
            const auto fileSize = static_cast<std::size_t>(input.tellg());
            input.seekg(0, std::ios::beg);

            std::array<char, 12> header{};
            if (!input.read(header.data(), static_cast<std::streamsize>(header.size())))
            {
                throw std::runtime_error("Failed to read AIFF header");
            }

            if (std::memcmp(header.data(), "FORM", 4) != 0 ||
                std::memcmp(header.data() + 8, "AIFF", 4) != 0)
            {
                throw std::runtime_error("Not an AIFF FORM file");
            }

            result.isAiff = true;
            result.formDataSize = detail::readBe32(header.data() + 4);
            if (result.formDataSize + 8 != fileSize)
            {
                throw std::runtime_error(
                    "FORM size field does not match file size");
            }

            std::size_t cursor = header.size();
            std::array<char, 8> chunkHeader{};
            while (input.read(chunkHeader.data(),
                              static_cast<std::streamsize>(chunkHeader.size())))
            {
                ChunkInfo chunk{};
                std::memcpy(chunk.id, chunkHeader.data(), 4);
                chunk.payloadSize = detail::readBe32(chunkHeader.data() + 4);
                chunk.headerOffset = cursor;
                chunk.payloadOffset = cursor + chunkHeader.size();
                chunk.paddedPayloadSize =
                    static_cast<std::size_t>(chunk.payloadSize) +
                    static_cast<std::size_t>(chunk.payloadSize & 1u);
                const auto chunkEnd = chunk.payloadOffset + chunk.paddedPayloadSize;
                if (chunkEnd > fileSize)
                {
                    throw std::runtime_error(
                        "AIFF chunk extends past end of file");
                }
                result.chunks.push_back(chunk);

                if (std::memcmp(chunk.id, "COMM", 4) == 0)
                {
                    ++result.commChunkCount;
                    std::vector<char> commData(chunk.payloadSize);
                    if (!commData.empty() &&
                        !input.read(commData.data(),
                                    static_cast<std::streamsize>(commData.size())))
                    {
                        throw std::runtime_error("Failed to read COMM chunk");
                    }
                    if (commData.size() >= 18)
                    {
                        result.channelCount =
                            static_cast<int>(detail::readBe16(commData.data()));
                        result.sampleFrameCount =
                            detail::readBe32(commData.data() + 2);
                        result.bitsPerSample = static_cast<int>(
                            detail::readBe16(commData.data() + 6));
                        const auto sampleRate =
                            detail::readExtended80(reinterpret_cast<
                                                   const unsigned char *>(
                                commData.data() + 8));
                        result.sampleRate = static_cast<int>(std::lround(sampleRate));
                        result.commSampleFrameCountOffset = chunk.payloadOffset + 2;
                        result.isPcm16 = result.bitsPerSample == 16;
                    }
                }
                else if (std::memcmp(chunk.id, "SSND", 4) == 0)
                {
                    ++result.ssndChunkCount;
                    if (chunk.payloadSize < 8)
                    {
                        throw std::runtime_error("SSND chunk is too small");
                    }

                    std::array<char, 8> ssndHeader{};
                    if (!input.read(ssndHeader.data(),
                                    static_cast<std::streamsize>(ssndHeader.size())))
                    {
                        throw std::runtime_error("Failed to read SSND header");
                    }

                    result.ssndOffset = detail::readBe32(ssndHeader.data());
                    result.ssndBlockSize = detail::readBe32(ssndHeader.data() + 4);

                    const auto soundDataOffset =
                        chunk.payloadOffset + 8 +
                        static_cast<std::size_t>(result.ssndOffset);
                    const auto soundDataEnd = chunk.payloadOffset + chunk.payloadSize;
                    if (soundDataOffset > soundDataEnd)
                    {
                        throw std::runtime_error(
                            "SSND sound data extends past chunk payload");
                    }

                    result.soundDataOffset = soundDataOffset;
                    result.soundDataSize = static_cast<std::uint32_t>(
                        soundDataEnd - soundDataOffset);

                    const auto remaining =
                        static_cast<std::streamoff>(chunk.payloadSize - 8);
                    input.seekg(remaining, std::ios::cur);
                }
                else
                {
                    input.seekg(static_cast<std::streamoff>(chunk.payloadSize),
                                std::ios::cur);
                }

                if ((chunk.payloadSize & 1u) != 0u)
                {
                    input.seekg(1, std::ios::cur);
                }
                if (!input)
                {
                    throw std::runtime_error("Failed while parsing AIFF chunks");
                }

                cursor = chunk.payloadOffset + chunk.paddedPayloadSize;
            }

            input.clear();
            return result;
        }
    };
} // namespace cupuacu::file::aiff
