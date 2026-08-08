#include "M4aParser.hpp"

#include <array>
#include <algorithm>
#include <cstddef>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace cupuacu::file::m4a
{
    namespace
    {
        using ByteRangeReader =
            std::function<Bytes(std::uint64_t offset, std::uint64_t size)>;

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
                    throw std::runtime_error("Invalid zero-length M4A packet");
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
                    "Chunk tables do not cover all M4A packets");
            }
            return sampleOffsets;
        }

        M4aParsedAlacFile parseAlacSampleDescription(const Bytes &bytes,
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

        struct DescriptorView
        {
            std::uint8_t tag = 0;
            std::uint64_t payloadOffset = 0;
            std::uint64_t payloadSize = 0;
            std::uint64_t endOffset = 0;
        };

        DescriptorView readDescriptor(const Bytes &bytes,
                                      const std::uint64_t offset,
                                      const std::uint64_t end)
        {
            if (offset >= end)
            {
                throw std::runtime_error("Truncated M4A descriptor");
            }
            DescriptorView descriptor{.tag = readU8(bytes, offset)};
            auto cursor = offset + 1;
            std::uint64_t size = 0;
            bool complete = false;
            for (int i = 0; i < 4; ++i)
            {
                if (cursor >= end)
                {
                    throw std::runtime_error("Truncated M4A descriptor length");
                }
                const auto byte = readU8(bytes, cursor++);
                size = (size << 7u) | (byte & 0x7fu);
                if ((byte & 0x80u) == 0)
                {
                    complete = true;
                    break;
                }
            }
            if (!complete || size > end - cursor)
            {
                throw std::runtime_error("Invalid M4A descriptor length");
            }
            descriptor.payloadOffset = cursor;
            descriptor.payloadSize = size;
            descriptor.endOffset = cursor + size;
            return descriptor;
        }

        Bytes parseAudioSpecificConfig(const Bytes &bytes, const AtomView &esds)
        {
            requireRange(bytes, esds.payloadOffset, 5, "Truncated esds atom");
            const auto es =
                readDescriptor(bytes, esds.payloadOffset + 4,
                               esds.payloadOffset + esds.payloadSize);
            if (es.tag != 0x03u || es.payloadSize < 3)
            {
                throw std::runtime_error("M4A ES descriptor missing");
            }

            auto cursor = es.payloadOffset + 2;
            const auto flags = readU8(bytes, cursor++);
            if ((flags & 0x80u) != 0)
            {
                cursor += 2;
            }
            if ((flags & 0x40u) != 0)
            {
                const auto urlLength = readU8(bytes, cursor++);
                cursor += urlLength;
            }
            if ((flags & 0x20u) != 0)
            {
                cursor += 2;
            }
            if (cursor > es.endOffset)
            {
                throw std::runtime_error("Truncated M4A ES descriptor flags");
            }

            const auto decoderConfig =
                readDescriptor(bytes, cursor, es.endOffset);
            if (decoderConfig.tag != 0x04u || decoderConfig.payloadSize < 13)
            {
                throw std::runtime_error("M4A decoder configuration missing");
            }
            if (readU8(bytes, decoderConfig.payloadOffset) != 0x40u ||
                (readU8(bytes, decoderConfig.payloadOffset + 1) >> 2u) != 0x05u)
            {
                throw std::runtime_error("M4A sample is not MPEG-4 AAC audio");
            }

            const auto specific =
                readDescriptor(bytes, decoderConfig.payloadOffset + 13,
                               decoderConfig.endOffset);
            if (specific.tag != 0x05u || specific.payloadSize < 2)
            {
                throw std::runtime_error("AAC AudioSpecificConfig missing");
            }
            return Bytes(bytes.begin() + static_cast<std::ptrdiff_t>(
                                             specific.payloadOffset),
                         bytes.begin() +
                             static_cast<std::ptrdiff_t>(specific.endOffset));
        }

        M4aParsedAacFile parseAacSampleDescription(const Bytes &bytes,
                                                   const AtomView &stsd)
        {
            requireRange(bytes, stsd.payloadOffset, 8, "Truncated stsd atom");
            if (readBe32(bytes, stsd.payloadOffset + 4) == 0)
            {
                throw std::runtime_error("M4A AAC sample description missing");
            }
            const auto entry = readAtom(bytes, stsd.payloadOffset + 8);
            if (entry.type != "mp4a")
            {
                throw std::runtime_error("M4A audio sample entry is not AAC");
            }
            requireRange(bytes, entry.payloadOffset, 28,
                         "Truncated AAC sample entry");
            const auto children =
                childrenInRange(bytes, entry.payloadOffset + 28,
                                entry.payloadOffset + entry.payloadSize);
            const auto esds = std::find_if(children.begin(), children.end(),
                                           [](const AtomView &atom)
                                           {
                                               return atom.type == "esds";
                                           });
            if (esds == children.end())
            {
                throw std::runtime_error("M4A AAC esds atom missing");
            }
            M4aParsedAacFile parsed;
            parsed.audioSpecificConfig = parseAudioSpecificConfig(bytes, *esds);

            std::uint32_t bitPosition = 0;
            auto readBits = [&](const std::uint32_t count)
            {
                std::uint32_t value = 0;
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    const auto byteIndex = bitPosition / 8u;
                    if (byteIndex >= parsed.audioSpecificConfig.size())
                    {
                        throw std::runtime_error(
                            "Truncated AAC AudioSpecificConfig");
                    }
                    const auto shift = 7u - (bitPosition % 8u);
                    value =
                        (value << 1u) |
                        ((parsed.audioSpecificConfig[byteIndex] >> shift) & 1u);
                    ++bitPosition;
                }
                return value;
            };
            const auto audioObjectType = readBits(5);
            if (audioObjectType != 2)
            {
                throw std::runtime_error(
                    "Unsupported AAC profile in M4A file (AAC-LC is required)");
            }
            constexpr std::array<std::uint32_t, 13> sampleRates{
                96000, 88200, 64000, 48000, 44100, 32000, 24000,
                22050, 16000, 12000, 11025, 8000,  7350};
            const auto frequencyIndex = readBits(4);
            if (frequencyIndex == 15)
            {
                parsed.sampleRate = readBits(24);
            }
            else if (frequencyIndex < sampleRates.size())
            {
                parsed.sampleRate = sampleRates[frequencyIndex];
            }
            const auto channelConfiguration = readBits(4);
            constexpr std::array<std::uint16_t, 8> channelCounts{0, 1, 2, 3,
                                                                 4, 5, 6, 8};
            if (parsed.sampleRate == 0 ||
                channelConfiguration >= channelCounts.size() ||
                channelCounts[channelConfiguration] == 0)
            {
                throw std::runtime_error(
                    "Unsupported AAC channel configuration");
            }
            parsed.channels = channelCounts[channelConfiguration];
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

        Bytes readByteRangeFromMemory(const Bytes &bytes,
                                      const std::uint64_t offset,
                                      const std::uint64_t size)
        {
            requireRange(bytes, offset, size, "M4A sample extends past file");
            return Bytes(
                bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
        }

        std::vector<cupuacu::DocumentMarker> parseChapterMarkers(
            const Bytes &bytes,
            const AtomView &moov,
            const AtomView &audioTrack,
            const ByteRangeReader &readByteRange)
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
                        throw std::runtime_error("M4A chapter timing table does not match sample table");
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
                        const auto sampleBytes =
                            readByteRange(sampleOffset, sampleSize);
                        if (sampleBytes.size() != sampleSize)
                        {
                            throw std::runtime_error(
                                "M4A chapter sample extends past file");
                        }

                        std::string label;
                        if (sampleSize >= 2)
                        {
                            const auto labelSize = std::min<std::uint32_t>(
                                readBe16(sampleBytes, 0), sampleSize - 2u);
                            label.reserve(labelSize);
                            for (std::uint32_t j = 0; j < labelSize; ++j)
                            {
                                label.push_back(static_cast<char>(
                                    readU8(sampleBytes, 2u + j)));
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

        std::optional<std::string>
        parseItunesFreeformValue(const Bytes &bytes, const AtomView &moov,
                                 const std::string &requestedName)
        {
            const auto udta = findChild(bytes, moov, "udta");
            if (!udta)
            {
                return std::nullopt;
            }
            const auto meta = findChild(bytes, *udta, "meta");
            if (!meta || meta->payloadSize < 4)
            {
                return std::nullopt;
            }
            const auto metaChildren =
                childrenInRange(bytes, meta->payloadOffset + 4,
                                meta->payloadOffset + meta->payloadSize);
            const auto ilst =
                std::find_if(metaChildren.begin(), metaChildren.end(),
                             [](const AtomView &atom)
                             {
                                 return atom.type == "ilst";
                             });
            if (ilst == metaChildren.end())
            {
                return std::nullopt;
            }
            const auto entries =
                childrenInRange(bytes, ilst->payloadOffset,
                                ilst->payloadOffset + ilst->payloadSize);
            for (const auto &entry : entries)
            {
                if (entry.type != "----")
                {
                    continue;
                }
                const auto children =
                    childrenInRange(bytes, entry.payloadOffset,
                                    entry.payloadOffset + entry.payloadSize);
                const auto name = std::find_if(children.begin(), children.end(),
                                               [](const AtomView &atom)
                                               {
                                                   return atom.type == "name";
                                               });
                const auto data = std::find_if(children.begin(), children.end(),
                                               [](const AtomView &atom)
                                               {
                                                   return atom.type == "data";
                                               });
                if (name == children.end() || data == children.end() ||
                    name->payloadSize < 4 || data->payloadSize < 8)
                {
                    continue;
                }
                const std::string nameValue(
                    bytes.begin() +
                        static_cast<std::ptrdiff_t>(name->payloadOffset + 4),
                    bytes.begin() +
                        static_cast<std::ptrdiff_t>(name->payloadOffset +
                                                    name->payloadSize));
                if (nameValue != requestedName)
                {
                    continue;
                }
                return std::string(bytes.begin() + static_cast<std::ptrdiff_t>(
                                                       data->payloadOffset + 8),
                                   bytes.begin() + static_cast<std::ptrdiff_t>(
                                                       data->payloadOffset +
                                                       data->payloadSize));
            }
            return std::nullopt;
        }

        void parseGaplessTrim(const Bytes &bytes, const AtomView &moov,
                              M4aParsedAacFile &parsed)
        {
            const auto value =
                parseItunesFreeformValue(bytes, moov, "iTunSMPB");
            if (!value)
            {
                return;
            }
            std::istringstream stream(*value);
            std::uint64_t reserved = 0;
            std::uint64_t priming = 0;
            std::uint64_t padding = 0;
            std::uint64_t originalFrames = 0;
            stream >> std::hex >> reserved >> priming >> padding >>
                originalFrames;
            if (!stream ||
                priming > std::numeric_limits<std::uint32_t>::max() ||
                padding > std::numeric_limits<std::uint32_t>::max() ||
                priming + padding > parsed.frameCount ||
                (originalFrames != 0 &&
                 originalFrames != parsed.frameCount - priming - padding))
            {
                return;
            }
            parsed.primingFrames = static_cast<std::uint32_t>(priming);
            parsed.paddingFrames = static_cast<std::uint32_t>(padding);
        }

        struct TopLevelFileLayout
        {
            std::uint64_t fileSize = 0;
            AtomView moov;
            AtomView mdat;
        };

        AtomView readFileAtom(std::ifstream &input,
                              const std::uint64_t offset,
                              const std::uint64_t fileSize)
        {
            if (offset > fileSize || fileSize - offset < 8)
            {
                throw std::runtime_error("Truncated M4A atom header");
            }

            std::array<std::uint8_t, 16> header{};
            input.clear();
            input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            input.read(reinterpret_cast<char *>(header.data()), 8);
            if (input.gcount() != 8)
            {
                throw std::runtime_error("Truncated M4A atom header");
            }

            const auto smallSize = readBe32(Bytes(header.begin(), header.end()),
                                            0);
            const auto type =
                std::string{static_cast<char>(header[4]),
                            static_cast<char>(header[5]),
                            static_cast<char>(header[6]),
                            static_cast<char>(header[7])};
            std::uint64_t size = smallSize;
            std::uint64_t payloadOffset = offset + 8;

            if (smallSize == 1)
            {
                if (fileSize - offset < 16)
                {
                    throw std::runtime_error("Truncated extended M4A atom");
                }
                input.read(reinterpret_cast<char *>(header.data() + 8), 8);
                if (input.gcount() != 8)
                {
                    throw std::runtime_error("Truncated extended M4A atom");
                }
                size = readBe64(Bytes(header.begin(), header.end()), 8);
                payloadOffset = offset + 16;
            }
            else if (smallSize == 0)
            {
                size = fileSize - offset;
            }

            if (size < payloadOffset - offset || size > fileSize - offset)
            {
                throw std::runtime_error("Invalid M4A atom size");
            }

            return AtomView{
                .type = type,
                .offset = offset,
                .size = size,
                .payloadOffset = payloadOffset,
                .payloadSize = size - (payloadOffset - offset),
            };
        }

        TopLevelFileLayout scanTopLevelFileLayout(std::ifstream &input)
        {
            input.clear();
            input.seekg(0, std::ios::end);
            const auto endPosition = input.tellg();
            if (endPosition < 0)
            {
                throw std::runtime_error("Failed to measure M4A file");
            }

            TopLevelFileLayout layout{};
            layout.fileSize = static_cast<std::uint64_t>(endPosition);
            bool haveMoov = false;
            bool haveMdat = false;
            for (std::uint64_t offset = 0; offset < layout.fileSize;)
            {
                const auto atom = readFileAtom(input, offset, layout.fileSize);
                if (atom.type == "moov")
                {
                    layout.moov = atom;
                    haveMoov = true;
                }
                else if (atom.type == "mdat")
                {
                    layout.mdat = atom;
                    haveMdat = true;
                }
                offset += atom.size;
            }

            if (!haveMoov)
            {
                throw std::runtime_error(
                    "Required top-level M4A atom missing: moov");
            }
            if (!haveMdat)
            {
                throw std::runtime_error(
                    "Required top-level M4A atom missing: mdat");
            }
            return layout;
        }

        Bytes readFileRange(std::ifstream &input,
                            const std::uint64_t offset,
                            const std::uint64_t size,
                            const std::uint64_t fileSize)
        {
            if (offset > fileSize || size > fileSize - offset)
            {
                throw std::runtime_error("M4A sample extends past file");
            }

            Bytes bytes(static_cast<std::size_t>(size));
            if (bytes.empty())
            {
                return bytes;
            }

            input.clear();
            input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            input.read(reinterpret_cast<char *>(bytes.data()),
                       static_cast<std::streamsize>(size));
            if (input.gcount() != static_cast<std::streamsize>(size))
            {
                throw std::runtime_error("M4A sample extends past file");
            }
            return bytes;
        }

        M4aParsedAlacFile parseAlacM4aFromMoov(
            const Bytes &moovBytes,
            const std::uint64_t mdatPayloadOffset,
            const std::uint64_t mdatPayloadSize,
            const ByteRangeReader &readByteRange)
        {
            const auto moov = requireTopLevel(moovBytes, "moov");
            const auto audioTrack =
                requireTrackByHandler(moovBytes, moov, "soun");
            const auto stbl = requireNested(
                moovBytes, audioTrack, {"mdia", "minf", "stbl"});

            const auto stsd = requireChild(moovBytes, stbl, "stsd");
            const auto stts = requireChild(moovBytes, stbl, "stts");
            const auto stsz = requireChild(moovBytes, stbl, "stsz");

            auto parsed = parseAlacSampleDescription(moovBytes, stsd);
            parsed.packetSizes = parsePacketSizes(moovBytes, stsz);
            std::uint32_t sttsFramesPerPacket = 0;
            parsed.packetFrameCounts = parsePacketFrameCounts(
                moovBytes, stts, sttsFramesPerPacket, parsed.frameCount);
            if (parsed.framesPerPacket == 0)
            {
                parsed.framesPerPacket = sttsFramesPerPacket;
            }
            if (parsed.packetFrameCounts.size() != parsed.packetSizes.size())
            {
                throw std::runtime_error(
                    "M4A timing table does not match packet table");
            }

            parsed.mdatPayloadOffset = mdatPayloadOffset;
            parsed.mdatPayloadSize = mdatPayloadSize;
            parsed.packetOffsets =
                buildSampleOffsets(moovBytes, stbl, parsed.packetSizes);
            const auto mdatEnd =
                parsed.mdatPayloadOffset + parsed.mdatPayloadSize;
            for (std::size_t i = 0; i < parsed.packetSizes.size(); ++i)
            {
                const auto packetOffset = parsed.packetOffsets[i];
                const auto packetSize = parsed.packetSizes[i];
                if (packetOffset > mdatEnd || packetSize > mdatEnd - packetOffset)
                {
                    throw std::runtime_error("ALAC packet extends outside mdat");
                }
            }

            parsed.markers = parseChapterMarkers(moovBytes, moov, audioTrack,
                                                 readByteRange);
            return parsed;
        }

        M4aParsedAacFile
        parseAacM4aFromMoov(const Bytes &moovBytes,
                            const std::uint64_t mdatPayloadOffset,
                            const std::uint64_t mdatPayloadSize,
                            const ByteRangeReader &readByteRange)
        {
            const auto moov = requireTopLevel(moovBytes, "moov");
            const auto audioTrack =
                requireTrackByHandler(moovBytes, moov, "soun");
            const auto stbl =
                requireNested(moovBytes, audioTrack, {"mdia", "minf", "stbl"});
            const auto stsd = requireChild(moovBytes, stbl, "stsd");
            const auto stts = requireChild(moovBytes, stbl, "stts");
            const auto stsz = requireChild(moovBytes, stbl, "stsz");

            auto parsed = parseAacSampleDescription(moovBytes, stsd);
            parsed.packetSizes = parsePacketSizes(moovBytes, stsz);
            parsed.packetFrameCounts = parsePacketFrameCounts(
                moovBytes, stts, parsed.framesPerPacket, parsed.frameCount);
            if (parsed.packetFrameCounts.size() != parsed.packetSizes.size())
            {
                throw std::runtime_error(
                    "M4A timing table does not match packet table");
            }
            parsed.mdatPayloadOffset = mdatPayloadOffset;
            parsed.mdatPayloadSize = mdatPayloadSize;
            parsed.packetOffsets =
                buildSampleOffsets(moovBytes, stbl, parsed.packetSizes);
            const auto mdatEnd = mdatPayloadOffset + mdatPayloadSize;
            for (std::size_t i = 0; i < parsed.packetSizes.size(); ++i)
            {
                if (parsed.packetOffsets[i] > mdatEnd ||
                    parsed.packetSizes[i] > mdatEnd - parsed.packetOffsets[i])
                {
                    throw std::runtime_error("AAC packet extends outside mdat");
                }
            }
            parseGaplessTrim(moovBytes, moov, parsed);
            parsed.markers =
                parseChapterMarkers(moovBytes, moov, audioTrack, readByteRange);
            return parsed;
        }

        M4aAudioCodec detectM4aAudioCodecFromMoov(const Bytes &moovBytes)
        {
            const auto moov = requireTopLevel(moovBytes, "moov");
            const auto audioTrack =
                requireTrackByHandler(moovBytes, moov, "soun");
            const auto stsd = requireNested(moovBytes, audioTrack,
                                            {"mdia", "minf", "stbl", "stsd"});
            requireRange(moovBytes, stsd.payloadOffset, 8,
                         "Truncated stsd atom");
            if (readBe32(moovBytes, stsd.payloadOffset + 4) == 0)
            {
                throw std::runtime_error(
                    "M4A audio sample description missing");
            }
            const auto entry = readAtom(moovBytes, stsd.payloadOffset + 8);
            if (entry.type == "alac")
            {
                return M4aAudioCodec::ALAC;
            }
            if (entry.type == "mp4a")
            {
                return M4aAudioCodec::AAC_LC;
            }
            throw std::runtime_error("Unsupported audio codec in M4A file");
        }
    } // namespace

    M4aParsedAlacFile parseAlacM4a(const Bytes &bytes)
    {
        const auto mdat = requireTopLevel(bytes, "mdat");
        return parseAlacM4aFromMoov(
            bytes, mdat.payloadOffset, mdat.payloadSize,
            [&](const std::uint64_t offset, const std::uint64_t size)
            { return readByteRangeFromMemory(bytes, offset, size); });
    }

    M4aParsedAlacFile parseAlacM4aFile(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("Failed to open M4A file: " + path.string());
        }
        return parseAlacM4aFile(input);
    }

    M4aParsedAlacFile parseAlacM4aFile(std::ifstream &input)
    {
        const auto layout = scanTopLevelFileLayout(input);
        const auto moovBytes =
            readFileRange(input, layout.moov.offset, layout.moov.size,
                          layout.fileSize);
        return parseAlacM4aFromMoov(
            moovBytes, layout.mdat.payloadOffset, layout.mdat.payloadSize,
            [&](const std::uint64_t offset, const std::uint64_t size)
            { return readFileRange(input, offset, size, layout.fileSize); });
    }

    M4aAudioCodec detectM4aAudioCodec(const Bytes &bytes)
    {
        return detectM4aAudioCodecFromMoov(bytes);
    }

    M4aAudioCodec detectM4aAudioCodecFile(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("Failed to open M4A file: " +
                                     path.string());
        }
        const auto layout = scanTopLevelFileLayout(input);
        const auto moovBytes = readFileRange(input, layout.moov.offset,
                                             layout.moov.size, layout.fileSize);
        return detectM4aAudioCodecFromMoov(moovBytes);
    }

    M4aParsedAacFile parseAacM4a(const Bytes &bytes)
    {
        const auto mdat = requireTopLevel(bytes, "mdat");
        return parseAacM4aFromMoov(
            bytes, mdat.payloadOffset, mdat.payloadSize,
            [&](const std::uint64_t offset, const std::uint64_t size)
            {
                return readByteRangeFromMemory(bytes, offset, size);
            });
    }

    M4aParsedAacFile parseAacM4aFile(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("Failed to open M4A file: " +
                                     path.string());
        }
        return parseAacM4aFile(input);
    }

    M4aParsedAacFile parseAacM4aFile(std::ifstream &input)
    {
        const auto layout = scanTopLevelFileLayout(input);
        const auto moovBytes = readFileRange(input, layout.moov.offset,
                                             layout.moov.size, layout.fileSize);
        return parseAacM4aFromMoov(
            moovBytes, layout.mdat.payloadOffset, layout.mdat.payloadSize,
            [&](const std::uint64_t offset, const std::uint64_t size)
            {
                return readFileRange(input, offset, size, layout.fileSize);
            });
    }
} // namespace cupuacu::file::m4a
