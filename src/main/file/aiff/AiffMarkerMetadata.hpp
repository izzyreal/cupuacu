#pragma once

#include "../../Document.hpp"
#include "../PcmPreservationIO.hpp"
#include "AiffParser.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace cupuacu::file::aiff
{
    namespace markers
    {
        namespace detail
        {
            inline void appendByte(std::vector<std::uint8_t> &bytes,
                                   const std::uint8_t value)
            {
                bytes.push_back(value);
            }

            inline void appendAscii(std::vector<std::uint8_t> &bytes,
                                    const char *text)
            {
                while (*text != '\0')
                {
                    appendByte(bytes, static_cast<std::uint8_t>(*text));
                    ++text;
                }
            }

            inline void appendBe16(std::vector<std::uint8_t> &bytes,
                                   const std::uint16_t value)
            {
                appendByte(bytes,
                           static_cast<std::uint8_t>((value >> 8) & 0xffu));
                appendByte(bytes, static_cast<std::uint8_t>(value & 0xffu));
            }

            inline void appendBe32(std::vector<std::uint8_t> &bytes,
                                   const std::uint32_t value)
            {
                appendByte(bytes,
                           static_cast<std::uint8_t>((value >> 24) & 0xffu));
                appendByte(bytes,
                           static_cast<std::uint8_t>((value >> 16) & 0xffu));
                appendByte(bytes,
                           static_cast<std::uint8_t>((value >> 8) & 0xffu));
                appendByte(bytes, static_cast<std::uint8_t>(value & 0xffu));
            }

            inline std::uint16_t markerStorageId(
                const cupuacu::DocumentMarker &marker, const std::uint16_t fallback)
            {
                if (marker.id == 0 ||
                    marker.id > static_cast<std::uint64_t>(
                                    std::numeric_limits<std::uint16_t>::max()))
                {
                    return fallback;
                }
                return static_cast<std::uint16_t>(marker.id);
            }

            inline std::vector<std::uint8_t>
            buildMarkChunkPayload(const std::vector<cupuacu::DocumentMarker> &markers)
            {
                std::vector<std::uint8_t> payload;
                appendBe16(payload, static_cast<std::uint16_t>(markers.size()));
                for (std::size_t i = 0; i < markers.size(); ++i)
                {
                    const auto &marker = markers[i];
                    const auto id = markerStorageId(
                        marker, static_cast<std::uint16_t>(i + 1));
                    appendBe16(payload, id);
                    appendBe32(payload, static_cast<std::uint32_t>(
                                            std::max<std::int64_t>(0, marker.frame)));

                    const std::size_t labelSize =
                        std::min<std::size_t>(marker.label.size(), 255);
                    appendByte(payload, static_cast<std::uint8_t>(labelSize));
                    for (std::size_t index = 0; index < labelSize; ++index)
                    {
                        appendByte(payload,
                                   static_cast<std::uint8_t>(marker.label[index]));
                    }
                    if (((1 + labelSize) & 1u) == 0u)
                    {
                        appendByte(payload, 0);
                    }
                }
                return payload;
            }

            inline void appendChunk(std::ostream &output, const char *chunkId,
                                    const std::vector<std::uint8_t> &payload)
            {
                output.write(chunkId, 4);
                const auto payloadSize = static_cast<std::uint32_t>(payload.size());
                const std::array<char, 4> sizeBytes = {
                    static_cast<char>((payloadSize >> 24) & 0xffu),
                    static_cast<char>((payloadSize >> 16) & 0xffu),
                    static_cast<char>((payloadSize >> 8) & 0xffu),
                    static_cast<char>(payloadSize & 0xffu),
                };
                output.write(sizeBytes.data(),
                             static_cast<std::streamsize>(sizeBytes.size()));
                if (!payload.empty())
                {
                    output.write(reinterpret_cast<const char *>(payload.data()),
                                 static_cast<std::streamsize>(payload.size()));
                }
                if ((payload.size() & 1u) != 0u)
                {
                    output.put('\0');
                }
            }
        } // namespace detail

        inline std::vector<cupuacu::DocumentMarker>
        readMarkers(std::istream &input, const ParsedFile &parsed)
        {
            const auto *markChunk = parsed.findChunk("MARK");
            if (markChunk == nullptr || markChunk->payloadSize < 2)
            {
                return {};
            }

            input.clear();
            input.seekg(static_cast<std::streamoff>(markChunk->payloadOffset),
                        std::ios::beg);
            std::vector<char> markData(markChunk->payloadSize);
            if (!input.read(markData.data(),
                            static_cast<std::streamsize>(markData.size())))
            {
                return {};
            }

            std::vector<cupuacu::DocumentMarker> markers;
            const auto markerCount =
                cupuacu::file::aiff::detail::readBe16(markData.data());
            std::size_t cursor = 2;
            markers.reserve(static_cast<std::size_t>(markerCount));

            for (std::uint16_t i = 0; i < markerCount; ++i)
            {
                if (cursor + 7 > markData.size())
                {
                    break;
                }

                const auto id = cupuacu::file::aiff::detail::readBe16(
                    markData.data() + cursor);
                const auto frame = static_cast<std::int64_t>(
                    cupuacu::file::aiff::detail::readBe32(markData.data() +
                                                          cursor + 2));
                const auto labelLength = static_cast<std::uint8_t>(
                    markData[cursor + 6]);
                cursor += 7;
                if (cursor + labelLength > markData.size())
                {
                    break;
                }

                std::string label(markData.data() + cursor,
                                  markData.data() + cursor + labelLength);
                cursor += labelLength;
                if (((1u + labelLength) & 1u) == 0u && cursor < markData.size())
                {
                    ++cursor;
                }

                markers.push_back(cupuacu::DocumentMarker{
                    .id = id,
                    .frame = frame,
                    .label = std::move(label),
                });
            }

            return markers;
        }

        inline std::vector<cupuacu::DocumentMarker>
        readMarkers(const std::filesystem::path &path)
        {
            auto input = cupuacu::file::aiff::detail::openInputFileStream(path);
            const auto parsed = AiffParser::parseStream(input, path);
            return readMarkers(input, parsed);
        }

        inline bool isMarkerChunk(const ChunkInfo &chunk)
        {
            return std::memcmp(chunk.id, "MARK", 4) == 0;
        }

        inline void writeMarkerChunks(
            std::ostream &output, const std::vector<cupuacu::DocumentMarker> &markers)
        {
            if (markers.empty())
            {
                return;
            }

            detail::appendChunk(output, "MARK",
                                detail::buildMarkChunkPayload(markers));
        }

        inline void rewriteFileWithMarkers(
            const std::filesystem::path &path,
            const std::vector<cupuacu::DocumentMarker> &markers)
        {
            cupuacu::file::writeFileAtomically(
                path,
                [&](const std::filesystem::path &temporaryPath)
                {
                    auto input = cupuacu::file::aiff::detail::openInputFileStream(
                        path);
                    auto output =
                        cupuacu::file::preservation::openOutputFileStream(
                            temporaryPath);
                    const auto parsed = AiffParser::parseStream(input, path);

                    cupuacu::file::preservation::copyByteRange(
                        input, output, 0, 12, "Failed to copy AIFF header");

                    bool insertedMarkers = false;
                    for (const auto &chunk : parsed.chunks)
                    {
                        if (!insertedMarkers &&
                            (isMarkerChunk(chunk) ||
                             std::memcmp(chunk.id, "SSND", 4) == 0))
                        {
                            writeMarkerChunks(output, markers);
                            insertedMarkers = true;
                        }

                        if (isMarkerChunk(chunk))
                        {
                            continue;
                        }

                        cupuacu::file::preservation::copyByteRange(
                            input, output, chunk.headerOffset,
                            8 + chunk.paddedPayloadSize,
                            "Failed to copy AIFF chunk");
                    }

                    if (!insertedMarkers)
                    {
                        writeMarkerChunks(output, markers);
                    }

                    const auto fileSizePosition = output.tellp();
                    const auto formDataSize = static_cast<std::uint32_t>(
                        static_cast<std::uint64_t>(fileSizePosition) - 8u);
                    output.seekp(4, std::ios::beg);
                    const std::array<char, 4> sizeBytes = {
                        static_cast<char>((formDataSize >> 24) & 0xffu),
                        static_cast<char>((formDataSize >> 16) & 0xffu),
                        static_cast<char>((formDataSize >> 8) & 0xffu),
                        static_cast<char>(formDataSize & 0xffu),
                    };
                    output.write(sizeBytes.data(),
                                 static_cast<std::streamsize>(sizeBytes.size()));
                    output.seekp(fileSizePosition, std::ios::beg);
                    output.flush();
                });
        }
    } // namespace markers
} // namespace cupuacu::file::aiff
