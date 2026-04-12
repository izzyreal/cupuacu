#pragma once

#include "../FileIo.hpp"
#include "WavTypes.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace cupuacu::file::wav
{
    namespace detail
    {
        inline std::uint16_t readLe16(const char *data)
        {
            return static_cast<std::uint16_t>(
                static_cast<unsigned char>(data[0]) |
                (static_cast<unsigned char>(data[1]) << 8));
        }

        inline std::uint32_t readLe32(const char *data)
        {
            return static_cast<std::uint32_t>(
                static_cast<unsigned char>(data[0]) |
                (static_cast<unsigned char>(data[1]) << 8) |
                (static_cast<unsigned char>(data[2]) << 16) |
                (static_cast<unsigned char>(data[3]) << 24));
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
    } // namespace detail

    class WavParser
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
                throw std::runtime_error("Failed to read WAV header");
            }

            if (std::memcmp(header.data(), "RIFF", 4) != 0 ||
                std::memcmp(header.data() + 8, "WAVE", 4) != 0)
            {
                throw std::runtime_error("Not a RIFF/WAVE file");
            }

            result.isWave = true;
            result.riffDataSize = detail::readLe32(header.data() + 4);
            if (result.riffDataSize + 8 != fileSize)
            {
                throw std::runtime_error(
                    "RIFF size field does not match file size");
            }

            std::size_t cursor = header.size();
            std::array<char, 8> chunkHeader{};
            while (input.read(chunkHeader.data(),
                              static_cast<std::streamsize>(chunkHeader.size())))
            {
                ChunkInfo chunk{};
                std::memcpy(chunk.id, chunkHeader.data(), 4);
                chunk.payloadSize = detail::readLe32(chunkHeader.data() + 4);
                chunk.headerOffset = cursor;
                chunk.payloadOffset = cursor + chunkHeader.size();
                chunk.paddedPayloadSize =
                    static_cast<std::size_t>(chunk.payloadSize) +
                    static_cast<std::size_t>(chunk.payloadSize & 1u);
                const auto chunkEnd = chunk.payloadOffset + chunk.paddedPayloadSize;
                if (chunkEnd > fileSize)
                {
                    throw std::runtime_error(
                        "WAV chunk extends past end of file");
                }
                result.chunks.push_back(chunk);

                if (std::memcmp(chunk.id, "fmt ", 4) == 0)
                {
                    ++result.fmtChunkCount;
                    std::vector<char> fmtData(chunk.payloadSize);
                    if (!input.read(fmtData.data(),
                                    static_cast<std::streamsize>(fmtData.size())))
                    {
                        throw std::runtime_error("Failed to read fmt chunk");
                    }
                    if (fmtData.size() >= 16)
                    {
                        const std::uint16_t audioFormat =
                            detail::readLe16(fmtData.data());
                        result.channelCount =
                            static_cast<int>(detail::readLe16(fmtData.data() + 2));
                        result.sampleRate =
                            static_cast<int>(detail::readLe32(fmtData.data() + 4));
                        result.bitsPerSample =
                            static_cast<int>(detail::readLe16(fmtData.data() + 14));
                        result.isPcm16 =
                            audioFormat == 1 && result.bitsPerSample == 16;
                    }
                }
                else if (std::memcmp(chunk.id, "data", 4) == 0)
                {
                    ++result.dataChunkCount;
                    input.seekg(static_cast<std::streamoff>(chunk.payloadSize),
                                std::ios::cur);
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
                    throw std::runtime_error("Failed while parsing WAV chunks");
                }

                cursor = chunk.payloadOffset + chunk.paddedPayloadSize;
            }

            input.clear();
            return result;
        }
    };
} // namespace cupuacu::file::wav
