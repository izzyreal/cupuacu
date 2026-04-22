#pragma once

#include "../../Document.hpp"
#include "../PcmPreservationIO.hpp"
#include "WavParser.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace cupuacu::file::wav
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

            inline void appendLe32(std::vector<std::uint8_t> &bytes,
                                   const std::uint32_t value)
            {
                appendByte(bytes, static_cast<std::uint8_t>(value & 0xffu));
                appendByte(bytes,
                           static_cast<std::uint8_t>((value >> 8) & 0xffu));
                appendByte(bytes,
                           static_cast<std::uint8_t>((value >> 16) & 0xffu));
                appendByte(bytes,
                           static_cast<std::uint8_t>((value >> 24) & 0xffu));
            }

            inline std::uint32_t markerStorageId(
                const cupuacu::DocumentMarker &marker, const std::uint32_t fallback)
            {
                if (marker.id == 0 ||
                    marker.id > static_cast<std::uint64_t>(
                                    std::numeric_limits<std::uint32_t>::max()))
                {
                    return fallback;
                }
                return static_cast<std::uint32_t>(marker.id);
            }

            inline std::vector<std::uint8_t>
            buildCueChunkPayload(const std::vector<cupuacu::DocumentMarker> &markers)
            {
                std::vector<std::uint8_t> payload;
                appendLe32(payload, static_cast<std::uint32_t>(markers.size()));
                for (std::size_t i = 0; i < markers.size(); ++i)
                {
                    const auto &marker = markers[i];
                    const auto id = markerStorageId(
                        marker, static_cast<std::uint32_t>(i + 1));
                    appendLe32(payload, id);
                    appendLe32(payload, 0);
                    appendAscii(payload, "data");
                    appendLe32(payload, 0);
                    appendLe32(payload, 0);
                    appendLe32(payload, static_cast<std::uint32_t>(
                                            std::max<std::int64_t>(0, marker.frame)));
                }
                return payload;
            }

            inline std::vector<std::uint8_t>
            buildListAdtlChunkPayload(
                const std::vector<cupuacu::DocumentMarker> &markers)
            {
                std::vector<std::uint8_t> payload;
                appendAscii(payload, "adtl");

                for (std::size_t i = 0; i < markers.size(); ++i)
                {
                    const auto &marker = markers[i];
                    if (marker.label.empty())
                    {
                        continue;
                    }

                    const auto id = markerStorageId(
                        marker, static_cast<std::uint32_t>(i + 1));
                    appendAscii(payload, "labl");

                    const std::uint32_t textSize = static_cast<std::uint32_t>(
                        marker.label.size() + 1);
                    const std::uint32_t subchunkSize = 4 + textSize;
                    appendLe32(payload, subchunkSize);
                    appendLe32(payload, id);
                    for (const char ch : marker.label)
                    {
                        appendByte(payload, static_cast<std::uint8_t>(ch));
                    }
                    appendByte(payload, 0);
                    if ((subchunkSize & 1u) != 0u)
                    {
                        appendByte(payload, 0);
                    }
                }

                return payload;
            }

            inline bool isAdtlListChunk(std::istream &input, const ChunkInfo &chunk)
            {
                if (chunk.payloadSize < 4)
                {
                    return false;
                }

                std::array<char, 4> listType{};
                input.clear();
                input.seekg(static_cast<std::streamoff>(chunk.payloadOffset),
                            std::ios::beg);
                if (!input.read(listType.data(),
                                static_cast<std::streamsize>(listType.size())))
                {
                    return false;
                }
                return std::memcmp(listType.data(), "adtl", 4) == 0;
            }

            inline void appendChunk(std::ostream &output, const char *chunkId,
                                    const std::vector<std::uint8_t> &payload)
            {
                output.write(chunkId, 4);
                const std::uint32_t payloadSize = static_cast<std::uint32_t>(
                    payload.size());
                output.write(reinterpret_cast<const char *>(&payloadSize), 4);
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
            std::unordered_map<std::uint32_t, std::size_t> markerIndexById;
            std::vector<cupuacu::DocumentMarker> markers;

            if (const auto *cueChunk = parsed.findChunk("cue "); cueChunk != nullptr)
            {
                if (cueChunk->payloadSize >= 4)
                {
                    input.clear();
                    input.seekg(static_cast<std::streamoff>(cueChunk->payloadOffset),
                                std::ios::beg);
                    std::vector<char> cueData(cueChunk->payloadSize);
                    if (input.read(cueData.data(),
                                   static_cast<std::streamsize>(cueData.size())))
                    {
                        const auto cueCount =
                            cupuacu::file::wav::detail::readLe32(cueData.data());
                        const std::size_t requiredSize =
                            4 + static_cast<std::size_t>(cueCount) * 24;
                        if (cueData.size() >= requiredSize)
                        {
                            markers.reserve(static_cast<std::size_t>(cueCount));
                            for (std::uint32_t i = 0; i < cueCount; ++i)
                            {
                                const char *entry =
                                    cueData.data() + 4 + static_cast<std::size_t>(i) * 24;
                                const auto id =
                                    cupuacu::file::wav::detail::readLe32(entry);
                                const auto frame = static_cast<std::int64_t>(
                                    cupuacu::file::wav::detail::readLe32(
                                        entry + 20));
                                markerIndexById[id] = markers.size();
                                markers.push_back(cupuacu::DocumentMarker{
                                    .id = id,
                                    .frame = frame,
                                    .label = {},
                                });
                            }
                        }
                    }
                }
            }

            for (const auto &chunk : parsed.chunks)
            {
                if (std::memcmp(chunk.id, "LIST", 4) != 0 ||
                    !detail::isAdtlListChunk(input, chunk))
                {
                    continue;
                }

                std::vector<char> listData(chunk.payloadSize);
                input.clear();
                input.seekg(static_cast<std::streamoff>(chunk.payloadOffset),
                            std::ios::beg);
                if (!input.read(listData.data(),
                                static_cast<std::streamsize>(listData.size())))
                {
                    continue;
                }

                std::size_t cursor = 4;
                while (cursor + 8 <= listData.size())
                {
                    const char *subchunkId = listData.data() + cursor;
                    const auto subchunkSize =
                        cupuacu::file::wav::detail::readLe32(listData.data() +
                                                             cursor + 4);
                    const std::size_t paddedSubchunkSize =
                        static_cast<std::size_t>(subchunkSize) +
                        static_cast<std::size_t>(subchunkSize & 1u);
                    const std::size_t nextCursor = cursor + 8 + paddedSubchunkSize;
                    if (nextCursor > listData.size())
                    {
                        break;
                    }

                    if (std::memcmp(subchunkId, "labl", 4) == 0 &&
                        subchunkSize >= 4)
                    {
                        const auto id =
                            cupuacu::file::wav::detail::readLe32(
                                listData.data() + cursor + 8);
                        const char *textStart = listData.data() + cursor + 12;
                        const std::size_t textCapacity =
                            static_cast<std::size_t>(subchunkSize - 4);
                        std::string label;
                        for (std::size_t i = 0; i < textCapacity; ++i)
                        {
                            const char ch = textStart[i];
                            if (ch == '\0')
                            {
                                break;
                            }
                            label.push_back(ch);
                        }

                        if (const auto it = markerIndexById.find(id);
                            it != markerIndexById.end())
                        {
                            markers[it->second].label = std::move(label);
                        }
                    }

                    cursor = nextCursor;
                }
            }

            return markers;
        }

        inline std::vector<cupuacu::DocumentMarker>
        readMarkers(const std::filesystem::path &path)
        {
            auto input = cupuacu::file::wav::detail::openInputFileStream(path);
            const auto parsed = WavParser::parseStream(input, path);
            return readMarkers(input, parsed);
        }

        inline bool isMarkerChunk(std::istream &input, const ChunkInfo &chunk)
        {
            if (std::memcmp(chunk.id, "cue ", 4) == 0)
            {
                return true;
            }
            if (std::memcmp(chunk.id, "LIST", 4) == 0)
            {
                return detail::isAdtlListChunk(input, chunk);
            }
            return false;
        }

        inline void writeMarkerChunks(
            std::ostream &output, const std::vector<cupuacu::DocumentMarker> &markers)
        {
            if (markers.empty())
            {
                return;
            }

            detail::appendChunk(output, "cue ",
                                detail::buildCueChunkPayload(markers));

            const auto listPayload = detail::buildListAdtlChunkPayload(markers);
            if (listPayload.size() > 4)
            {
                detail::appendChunk(output, "LIST", listPayload);
            }
        }

        inline void rewriteFileWithMarkers(
            const std::filesystem::path &path,
            const std::vector<cupuacu::DocumentMarker> &markers)
        {
            cupuacu::file::writeFileAtomically(
                path,
                [&](const std::filesystem::path &temporaryPath)
                {
                    auto input =
                        cupuacu::file::wav::detail::openInputFileStream(path);
                    auto output =
                        cupuacu::file::preservation::openOutputFileStream(
                            temporaryPath);
                    const auto parsed = WavParser::parseStream(input, path);

                    cupuacu::file::preservation::copyByteRange(
                        input, output, 0, 12, "Failed to copy WAV header");

                    bool insertedMarkers = false;
                    for (const auto &chunk : parsed.chunks)
                    {
                        if (!insertedMarkers &&
                            (isMarkerChunk(input, chunk) ||
                             std::memcmp(chunk.id, "data", 4) == 0))
                        {
                            writeMarkerChunks(output, markers);
                            insertedMarkers = true;
                        }

                        if (isMarkerChunk(input, chunk))
                        {
                            continue;
                        }

                        cupuacu::file::preservation::copyByteRange(
                            input, output, chunk.headerOffset,
                            8 + chunk.paddedPayloadSize,
                            "Failed to copy WAV chunk");
                    }

                    if (!insertedMarkers)
                    {
                        writeMarkerChunks(output, markers);
                    }

                    const auto fileSizePosition = output.tellp();
                    const auto riffDataSize = static_cast<std::uint32_t>(
                        static_cast<std::uint64_t>(fileSizePosition) - 8u);
                    output.seekp(4, std::ios::beg);
                    output.write(reinterpret_cast<const char *>(&riffDataSize), 4);
                    output.seekp(fileSizePosition, std::ios::beg);
                    output.flush();
                });
        }
    } // namespace markers
} // namespace cupuacu::file::wav
