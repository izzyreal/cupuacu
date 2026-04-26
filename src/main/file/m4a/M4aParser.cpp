#include "M4aParser.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace cupuacu::file::m4a
{
    namespace
    {
        struct AtomView
        {
            std::string type;
            std::uint64_t offset = 0;
            std::uint64_t size = 0;
            std::uint64_t payloadOffset = 0;
            std::uint64_t payloadSize = 0;
        };

        std::uint8_t readU8(const Bytes &bytes, const std::uint64_t offset)
        {
            if (offset >= bytes.size())
            {
                throw std::runtime_error("M4A atom extends past end of file");
            }
            return bytes[static_cast<std::size_t>(offset)];
        }

        std::uint16_t readBe16(const Bytes &bytes, const std::uint64_t offset)
        {
            return static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(readU8(bytes, offset)) << 8u) |
                static_cast<std::uint16_t>(readU8(bytes, offset + 1)));
        }

        std::uint32_t readBe32(const Bytes &bytes, const std::uint64_t offset)
        {
            return (static_cast<std::uint32_t>(readU8(bytes, offset)) << 24u) |
                   (static_cast<std::uint32_t>(readU8(bytes, offset + 1))
                    << 16u) |
                   (static_cast<std::uint32_t>(readU8(bytes, offset + 2))
                    << 8u) |
                   static_cast<std::uint32_t>(readU8(bytes, offset + 3));
        }

        std::uint64_t readBe64(const Bytes &bytes, const std::uint64_t offset)
        {
            return (static_cast<std::uint64_t>(readBe32(bytes, offset)) << 32u) |
                   static_cast<std::uint64_t>(readBe32(bytes, offset + 4));
        }

        std::string readFourCc(const Bytes &bytes, const std::uint64_t offset)
        {
            std::string value;
            value.reserve(4);
            for (std::uint64_t i = 0; i < 4; ++i)
            {
                value.push_back(static_cast<char>(readU8(bytes, offset + i)));
            }
            return value;
        }

        void requireRange(const Bytes &bytes,
                          const std::uint64_t offset,
                          const std::uint64_t size,
                          const char *message)
        {
            if (offset > bytes.size() || size > bytes.size() - offset)
            {
                throw std::runtime_error(message);
            }
        }

        AtomView readAtom(const Bytes &bytes, const std::uint64_t offset)
        {
            requireRange(bytes, offset, 8, "Truncated M4A atom header");

            const auto smallSize = readBe32(bytes, offset);
            const auto type = readFourCc(bytes, offset + 4);
            std::uint64_t size = smallSize;
            std::uint64_t payloadOffset = offset + 8;

            if (smallSize == 1)
            {
                requireRange(bytes, offset, 16, "Truncated extended M4A atom");
                size = readBe64(bytes, offset + 8);
                payloadOffset = offset + 16;
            }
            else if (smallSize == 0)
            {
                size = static_cast<std::uint64_t>(bytes.size()) - offset;
            }

            if (size < payloadOffset - offset)
            {
                throw std::runtime_error("Invalid M4A atom size");
            }
            requireRange(bytes, offset, size, "M4A atom extends past end of file");

            return AtomView{
                .type = type,
                .offset = offset,
                .size = size,
                .payloadOffset = payloadOffset,
                .payloadSize = size - (payloadOffset - offset),
            };
        }

        std::vector<AtomView> childrenInRange(const Bytes &bytes,
                                              const std::uint64_t begin,
                                              const std::uint64_t end)
        {
            if (begin > end || end > bytes.size())
            {
                throw std::runtime_error("Invalid M4A child atom range");
            }

            std::vector<AtomView> atoms;
            for (auto offset = begin; offset < end;)
            {
                const auto atom = readAtom(bytes, offset);
                if (atom.offset + atom.size > end)
                {
                    throw std::runtime_error("M4A child atom extends past parent");
                }
                atoms.push_back(atom);
                offset += atom.size;
            }
            return atoms;
        }

        std::optional<AtomView> findChild(const Bytes &bytes,
                                          const AtomView &parent,
                                          const std::string &type)
        {
            const auto children =
                childrenInRange(bytes, parent.payloadOffset,
                                parent.payloadOffset + parent.payloadSize);
            const auto it =
                std::find_if(children.begin(), children.end(),
                             [&](const AtomView &atom)
                             { return atom.type == type; });
            if (it == children.end())
            {
                return std::nullopt;
            }
            return *it;
        }

        AtomView requireChild(const Bytes &bytes,
                              const AtomView &parent,
                              const std::string &type)
        {
            const auto atom = findChild(bytes, parent, type);
            if (!atom.has_value())
            {
                throw std::runtime_error("Required M4A atom missing: " + type);
            }
            return *atom;
        }

        AtomView requireNested(const Bytes &bytes,
                               const AtomView &root,
                               const std::vector<std::string> &path)
        {
            auto current = root;
            for (const auto &type : path)
            {
                current = requireChild(bytes, current, type);
            }
            return current;
        }

        std::vector<std::uint32_t> parsePacketSizes(const Bytes &bytes,
                                                    const AtomView &stsz)
        {
            requireRange(bytes, stsz.payloadOffset, 12, "Truncated stsz atom");
            const auto sampleSize = readBe32(bytes, stsz.payloadOffset + 4);
            const auto sampleCount = readBe32(bytes, stsz.payloadOffset + 8);

            std::vector<std::uint32_t> packetSizes;
            packetSizes.reserve(sampleCount);
            if (sampleSize != 0)
            {
                packetSizes.assign(sampleCount, sampleSize);
                return packetSizes;
            }

            requireRange(bytes, stsz.payloadOffset + 12,
                         static_cast<std::uint64_t>(sampleCount) * 4u,
                         "Truncated stsz packet table");
            for (std::uint32_t i = 0; i < sampleCount; ++i)
            {
                packetSizes.push_back(
                    readBe32(bytes, stsz.payloadOffset + 12u + i * 4u));
            }
            return packetSizes;
        }

        std::vector<std::uint32_t> parsePacketFrameCounts(
            const Bytes &bytes,
            const AtomView &stts,
            std::uint32_t &framesPerPacket,
            std::uint32_t &frameCount)
        {
            requireRange(bytes, stts.payloadOffset, 8, "Truncated stts atom");
            const auto entryCount = readBe32(bytes, stts.payloadOffset + 4);
            requireRange(bytes, stts.payloadOffset + 8,
                         static_cast<std::uint64_t>(entryCount) * 8u,
                         "Truncated stts entry table");

            std::uint64_t totalFrameCount = 0;
            framesPerPacket = 0;
            std::vector<std::uint32_t> packetFrameCounts;
            for (std::uint32_t i = 0; i < entryCount; ++i)
            {
                const auto entryOffset = stts.payloadOffset + 8u + i * 8u;
                const auto sampleCount = readBe32(bytes, entryOffset);
                const auto sampleDelta = readBe32(bytes, entryOffset + 4);
                if (sampleDelta == 0)
                {
                    throw std::runtime_error("Invalid zero-length ALAC packet");
                }
                if (i == 0)
                {
                    framesPerPacket = sampleDelta;
                }
                totalFrameCount += static_cast<std::uint64_t>(sampleCount) *
                                   static_cast<std::uint64_t>(sampleDelta);
                if (totalFrameCount > std::numeric_limits<std::uint32_t>::max())
                {
                    throw std::runtime_error("M4A frame count exceeds importer limit");
                }
                packetFrameCounts.insert(packetFrameCounts.end(), sampleCount,
                                         sampleDelta);
            }
            frameCount = static_cast<std::uint32_t>(totalFrameCount);
            return packetFrameCounts;
        }

        std::vector<std::uint64_t> parseChunkOffsets(const Bytes &bytes,
                                                     const AtomView &stbl)
        {
            if (const auto stco = findChild(bytes, stbl, "stco"))
            {
                requireRange(bytes, stco->payloadOffset, 8, "Truncated stco atom");
                const auto entryCount = readBe32(bytes, stco->payloadOffset + 4);
                requireRange(bytes, stco->payloadOffset + 8,
                             static_cast<std::uint64_t>(entryCount) * 4u,
                             "Truncated stco entry table");
                std::vector<std::uint64_t> chunkOffsets;
                chunkOffsets.reserve(entryCount);
                for (std::uint32_t i = 0; i < entryCount; ++i)
                {
                    chunkOffsets.push_back(
                        readBe32(bytes, stco->payloadOffset + 8u + i * 4u));
                }
                return chunkOffsets;
            }

            const auto co64 = requireChild(bytes, stbl, "co64");
            requireRange(bytes, co64.payloadOffset, 8, "Truncated co64 atom");
            const auto entryCount = readBe32(bytes, co64.payloadOffset + 4);
            requireRange(bytes, co64.payloadOffset + 8,
                         static_cast<std::uint64_t>(entryCount) * 8u,
                         "Truncated co64 entry table");
            std::vector<std::uint64_t> chunkOffsets;
            chunkOffsets.reserve(entryCount);
            for (std::uint32_t i = 0; i < entryCount; ++i)
            {
                chunkOffsets.push_back(
                    readBe64(bytes, co64.payloadOffset + 8u + i * 8u));
            }
            return chunkOffsets;
        }

        struct SampleToChunkEntry
        {
            std::uint32_t firstChunk = 0;
            std::uint32_t samplesPerChunk = 0;
            std::uint32_t sampleDescriptionIndex = 0;
        };

        std::vector<SampleToChunkEntry>
        parseSampleToChunkEntries(const Bytes &bytes, const AtomView &stsc)
        {
            requireRange(bytes, stsc.payloadOffset, 8, "Truncated stsc atom");
            const auto entryCount = readBe32(bytes, stsc.payloadOffset + 4);
            requireRange(bytes, stsc.payloadOffset + 8,
                         static_cast<std::uint64_t>(entryCount) * 12u,
                         "Truncated stsc entry table");

            std::vector<SampleToChunkEntry> entries;
            entries.reserve(entryCount);
            std::uint32_t previousFirstChunk = 0;
            for (std::uint32_t i = 0; i < entryCount; ++i)
            {
                const auto entryOffset = stsc.payloadOffset + 8u + i * 12u;
                const auto firstChunk = readBe32(bytes, entryOffset);
                const auto samplesPerChunk = readBe32(bytes, entryOffset + 4);
                const auto sampleDescriptionIndex =
                    readBe32(bytes, entryOffset + 8);
                if (firstChunk == 0 || samplesPerChunk == 0 ||
                    sampleDescriptionIndex == 0)
                {
                    throw std::runtime_error("Invalid stsc entry");
                }
                if (i != 0 && firstChunk <= previousFirstChunk)
                {
                    throw std::runtime_error("stsc entries must be ordered");
                }
                previousFirstChunk = firstChunk;
                entries.push_back({.firstChunk = firstChunk,
                                   .samplesPerChunk = samplesPerChunk,
                                   .sampleDescriptionIndex =
                                       sampleDescriptionIndex});
            }
            return entries;
        }

        std::vector<std::uint64_t>
        buildSampleOffsets(const Bytes &bytes,
                           const AtomView &stbl,
                           const std::vector<std::uint32_t> &sampleSizes)
        {
            if (sampleSizes.empty())
            {
                return {};
            }

            const auto stsc = requireChild(bytes, stbl, "stsc");
            const auto entries = parseSampleToChunkEntries(bytes, stsc);
            const auto chunkOffsets = parseChunkOffsets(bytes, stbl);
            if (entries.empty())
            {
                throw std::runtime_error("Sample-to-chunk table is empty");
            }
            if (chunkOffsets.empty())
            {
                throw std::runtime_error("Chunk-offset table is empty");
            }

            std::vector<std::uint64_t> sampleOffsets;
            sampleOffsets.reserve(sampleSizes.size());

            std::size_t entryIndex = 0;
            std::size_t sampleIndex = 0;
            for (std::size_t chunkIndex = 0; chunkIndex < chunkOffsets.size();
                 ++chunkIndex)
            {
                while (entryIndex + 1 < entries.size() &&
                       chunkIndex + 1 >= entries[entryIndex + 1].firstChunk)
                {
                    ++entryIndex;
                }

                std::uint64_t sampleOffset = chunkOffsets[chunkIndex];
                const auto samplesPerChunk = entries[entryIndex].samplesPerChunk;
                for (std::uint32_t i = 0;
                     i < samplesPerChunk && sampleIndex < sampleSizes.size();
                     ++i, ++sampleIndex)
                {
                    sampleOffsets.push_back(sampleOffset);
                    sampleOffset += sampleSizes[sampleIndex];
                }
            }

            if (sampleOffsets.size() != sampleSizes.size())
            {
                throw std::runtime_error(
                    "Chunk tables do not cover all ALAC packets");
            }
            return sampleOffsets;
        }

        M4aParsedAlacFile parseSampleDescription(const Bytes &bytes,
                                                 const AtomView &stsd)
        {
            requireRange(bytes, stsd.payloadOffset, 8, "Truncated stsd atom");
            const auto entryCount = readBe32(bytes, stsd.payloadOffset + 4);
            if (entryCount == 0)
            {
                throw std::runtime_error("M4A ALAC sample description missing");
            }

            const auto entry = readAtom(bytes, stsd.payloadOffset + 8);
            if (entry.type != "alac")
            {
                throw std::runtime_error("M4A audio sample entry is not ALAC");
            }
            requireRange(bytes, entry.payloadOffset, 28,
                         "Truncated ALAC sample entry");

            M4aParsedAlacFile parsed;
            parsed.channels = readBe16(bytes, entry.payloadOffset + 16);
            parsed.bitDepth = readBe16(bytes, entry.payloadOffset + 18);
            parsed.sampleRate = readBe32(bytes, entry.payloadOffset + 24) >> 16u;

            const auto childrenBegin = entry.payloadOffset + 28;
            const auto childrenEnd = entry.payloadOffset + entry.payloadSize;
            const auto children = childrenInRange(bytes, childrenBegin, childrenEnd);
            const auto alacChild =
                std::find_if(children.begin(), children.end(),
                             [](const AtomView &atom)
                             { return atom.type == "alac"; });
            if (alacChild == children.end())
            {
                throw std::runtime_error("M4A ALAC magic cookie missing");
            }
            requireRange(bytes, alacChild->payloadOffset, 4,
                         "Truncated ALAC magic cookie atom");

            const auto cookieOffset = alacChild->payloadOffset + 4;
            const auto cookieSize = alacChild->payloadSize - 4;
            if (cookieSize < 24)
            {
                throw std::runtime_error("Truncated ALAC magic cookie");
            }
            parsed.magicCookie.assign(
                bytes.begin() + static_cast<std::ptrdiff_t>(cookieOffset),
                bytes.begin() +
                    static_cast<std::ptrdiff_t>(cookieOffset + cookieSize));

            parsed.framesPerPacket = readBe32(parsed.magicCookie, 0);
            parsed.bitDepth = readU8(parsed.magicCookie, 5);
            parsed.channels = readU8(parsed.magicCookie, 9);
            parsed.sampleRate = readBe32(parsed.magicCookie, 20);
            if (parsed.framesPerPacket == 0 ||
                parsed.framesPerPacket > 1'048'576u ||
                parsed.sampleRate == 0 || parsed.sampleRate > 768'000u ||
                parsed.channels == 0 || parsed.channels > 8 ||
                (parsed.bitDepth != 16 && parsed.bitDepth != 20 &&
                 parsed.bitDepth != 24 && parsed.bitDepth != 32))
            {
                throw std::runtime_error("Invalid ALAC magic cookie");
            }
            return parsed;
        }

        AtomView requireTopLevel(const Bytes &bytes, const std::string &type)
        {
            const auto atoms = childrenInRange(bytes, 0, bytes.size());
            const auto it =
                std::find_if(atoms.begin(), atoms.end(),
                             [&](const AtomView &atom)
                             { return atom.type == type; });
            if (it == atoms.end())
            {
                throw std::runtime_error("Required top-level M4A atom missing: " +
                                         type);
            }
            return *it;
        }

        std::string parseHandlerType(const Bytes &bytes, const AtomView &trak);

        AtomView requireTrackByHandler(const Bytes &bytes,
                                       const AtomView &moov,
                                       const std::string &handlerType)
        {
            const auto children =
                childrenInRange(bytes, moov.payloadOffset,
                                moov.payloadOffset + moov.payloadSize);
            for (const auto &child : children)
            {
                if (child.type != "trak")
                {
                    continue;
                }
                if (parseHandlerType(bytes, child) == handlerType)
                {
                    return child;
                }
            }
            throw std::runtime_error("Required M4A track missing for handler: " +
                                     handlerType);
        }

        std::uint32_t parseTrackId(const Bytes &bytes, const AtomView &trak)
        {
            const auto tkhd = requireChild(bytes, trak, "tkhd");
            requireRange(bytes, tkhd.payloadOffset, 20, "Truncated tkhd atom");
            return readBe32(bytes, tkhd.payloadOffset + 12);
        }

        std::string parseHandlerType(const Bytes &bytes, const AtomView &trak)
        {
            const auto hdlr = requireNested(bytes, trak, {"mdia", "hdlr"});
            requireRange(bytes, hdlr.payloadOffset, 12, "Truncated hdlr atom");
            return readFourCc(bytes, hdlr.payloadOffset + 8);
        }

        std::vector<std::uint32_t> parseChapterTrackIds(
            const Bytes &bytes,
            const AtomView &trak)
        {
            const auto tref = findChild(bytes, trak, "tref");
            if (!tref.has_value())
            {
                return {};
            }

            const auto chap = findChild(bytes, *tref, "chap");
            if (!chap.has_value())
            {
                return {};
            }

            if ((chap->payloadSize % 4u) != 0u)
            {
                throw std::runtime_error("Invalid M4A chapter reference atom");
            }

            std::vector<std::uint32_t> trackIds;
            for (std::uint64_t offset = chap->payloadOffset;
                 offset < chap->payloadOffset + chap->payloadSize;
                 offset += 4u)
            {
                trackIds.push_back(readBe32(bytes, offset));
            }
            return trackIds;
        }

        std::uint32_t parseInitialEmptyEditDuration(const Bytes &bytes,
                                                    const AtomView &trak)
        {
            const auto edts = findChild(bytes, trak, "edts");
            if (!edts.has_value())
            {
                return 0;
            }
            const auto elst = findChild(bytes, *edts, "elst");
            if (!elst.has_value())
            {
                return 0;
            }

            requireRange(bytes, elst->payloadOffset, 8, "Truncated elst atom");
            const auto version = readU8(bytes, elst->payloadOffset);
            const auto entryCount = readBe32(bytes, elst->payloadOffset + 4);
            if (entryCount == 0)
            {
                return 0;
            }

            if (version == 0)
            {
                requireRange(bytes, elst->payloadOffset + 8, 12,
                             "Truncated elst entry");
                const auto duration = readBe32(bytes, elst->payloadOffset + 8);
                const auto mediaTime = readBe32(bytes, elst->payloadOffset + 12);
                return mediaTime == 0xffffffffu ? duration : 0;
            }

            requireRange(bytes, elst->payloadOffset + 8, 20,
                         "Truncated elst entry");
            const auto duration = readBe64(bytes, elst->payloadOffset + 8);
            const auto mediaTime = readBe64(bytes, elst->payloadOffset + 16);
            if (mediaTime == std::numeric_limits<std::uint64_t>::max() &&
                duration <= std::numeric_limits<std::uint32_t>::max())
            {
                return static_cast<std::uint32_t>(duration);
            }
            return 0;
        }

        std::vector<std::uint32_t> parseSampleDurations(const Bytes &bytes,
                                                        const AtomView &stts)
        {
            requireRange(bytes, stts.payloadOffset, 8, "Truncated stts atom");
            const auto entryCount = readBe32(bytes, stts.payloadOffset + 4);
            requireRange(bytes, stts.payloadOffset + 8,
                         static_cast<std::uint64_t>(entryCount) * 8u,
                         "Truncated stts entry table");

            std::vector<std::uint32_t> durations;
            for (std::uint32_t i = 0; i < entryCount; ++i)
            {
                const auto entryOffset = stts.payloadOffset + 8u + i * 8u;
                const auto sampleCount = readBe32(bytes, entryOffset);
                const auto sampleDelta = readBe32(bytes, entryOffset + 4);
                durations.insert(durations.end(), sampleCount, sampleDelta);
            }
            return durations;
        }

        std::vector<cupuacu::DocumentMarker> parseChapterMarkers(
            const Bytes &bytes,
            const AtomView &moov,
            const AtomView &audioTrack)
        {
            const auto referencedTrackIds = parseChapterTrackIds(bytes, audioTrack);
            if (referencedTrackIds.empty())
            {
                return {};
            }

            const auto tracks =
                childrenInRange(bytes, moov.payloadOffset,
                                moov.payloadOffset + moov.payloadSize);
            for (const auto &trackId : referencedTrackIds)
            {
                for (const auto &trak : tracks)
                {
                    if (trak.type != "trak" || parseTrackId(bytes, trak) != trackId ||
                        parseHandlerType(bytes, trak) != "text")
                    {
                        continue;
                    }

                    const auto stbl =
                        requireNested(bytes, trak, {"mdia", "minf", "stbl"});
                    const auto stts = requireChild(bytes, stbl, "stts");
                    const auto stsz = requireChild(bytes, stbl, "stsz");
                    const auto durations = parseSampleDurations(bytes, stts);
                    const auto sampleSizes = parsePacketSizes(bytes, stsz);
                    if (durations.size() != sampleSizes.size())
                    {
                        throw std::runtime_error(
                            "M4A chapter timing table does not match sample table");
                    }
                    const auto sampleOffsets =
                        buildSampleOffsets(bytes, stbl, sampleSizes);
                    if (sampleOffsets.size() != sampleSizes.size())
                    {
                        throw std::runtime_error(
                            "M4A chapter sample table does not match offsets");
                    }

                    std::vector<cupuacu::DocumentMarker> markers;
                    markers.reserve(sampleSizes.size());
                    std::uint64_t frame =
                        parseInitialEmptyEditDuration(bytes, trak);
                    for (std::size_t i = 0; i < sampleSizes.size(); ++i)
                    {
                        const auto sampleSize = sampleSizes[i];
                        const auto sampleOffset = sampleOffsets[i];
                        requireRange(bytes, sampleOffset, sampleSize,
                                     "M4A chapter sample extends past file");

                        std::string label;
                        if (sampleSize >= 2)
                        {
                            const auto labelSize = std::min<std::uint32_t>(
                                readBe16(bytes, sampleOffset), sampleSize - 2u);
                            label.reserve(labelSize);
                            for (std::uint32_t j = 0; j < labelSize; ++j)
                            {
                                label.push_back(static_cast<char>(
                                    readU8(bytes, sampleOffset + 2u + j)));
                            }
                        }
                        if (frame <=
                            static_cast<std::uint64_t>(
                                std::numeric_limits<std::int64_t>::max()))
                        {
                            markers.push_back(cupuacu::DocumentMarker{
                                .id = static_cast<std::uint64_t>(markers.size() + 1),
                                .frame = static_cast<std::int64_t>(frame),
                                .label = std::move(label),
                            });
                        }
                        frame += durations[i];
                    }
                    return markers;
                }
            }
            return {};
        }
    } // namespace

    M4aParsedAlacFile parseAlacM4a(const Bytes &bytes)
    {
        const auto mdat = requireTopLevel(bytes, "mdat");
        const auto moov = requireTopLevel(bytes, "moov");
        const auto audioTrack = requireTrackByHandler(bytes, moov, "soun");
        const auto stbl = requireNested(
            bytes, audioTrack, {"mdia", "minf", "stbl"});

        const auto stsd = requireChild(bytes, stbl, "stsd");
        const auto stts = requireChild(bytes, stbl, "stts");
        const auto stsz = requireChild(bytes, stbl, "stsz");

        auto parsed = parseSampleDescription(bytes, stsd);
        parsed.packetSizes = parsePacketSizes(bytes, stsz);
        std::uint32_t sttsFramesPerPacket = 0;
        parsed.packetFrameCounts = parsePacketFrameCounts(
            bytes, stts, sttsFramesPerPacket, parsed.frameCount);
        if (parsed.framesPerPacket == 0)
        {
            parsed.framesPerPacket = sttsFramesPerPacket;
        }
        if (parsed.packetFrameCounts.size() != parsed.packetSizes.size())
        {
            throw std::runtime_error("M4A timing table does not match packet table");
        }

        parsed.mdatPayloadOffset = mdat.payloadOffset;
        parsed.mdatPayloadSize = mdat.payloadSize;
        parsed.packetOffsets = buildSampleOffsets(bytes, stbl, parsed.packetSizes);
        const auto mdatEnd = parsed.mdatPayloadOffset + parsed.mdatPayloadSize;
        for (std::size_t i = 0; i < parsed.packetSizes.size(); ++i)
        {
            const auto packetOffset = parsed.packetOffsets[i];
            const auto packetSize = parsed.packetSizes[i];
            if (packetOffset > mdatEnd || packetSize > mdatEnd - packetOffset)
            {
                throw std::runtime_error("ALAC packet extends outside mdat");
            }
        }

        parsed.markers = parseChapterMarkers(bytes, moov, audioTrack);

        return parsed;
    }

    M4aParsedAlacFile parseAlacM4aFile(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("Failed to open M4A file: " + path.string());
        }

        const Bytes bytes{std::istreambuf_iterator<char>(input),
                          std::istreambuf_iterator<char>()};
        return parseAlacM4a(bytes);
    }
} // namespace cupuacu::file::m4a
