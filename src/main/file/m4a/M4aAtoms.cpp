#include "M4aAtoms.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace cupuacu::file::m4a
{
    namespace
    {
        void validateFourCc(const std::string_view fourCc)
        {
            if (fourCc.size() != 4)
            {
                throw std::invalid_argument("MP4 atom type must be four bytes");
            }
        }

        void appendBytes(Bytes &bytes, const Bytes &payload)
        {
            bytes.insert(bytes.end(), payload.begin(), payload.end());
        }

        struct ChapterSample
        {
            std::uint32_t frame = 0;
            std::uint32_t duration = 0;
            Bytes bytes;
        };

        std::vector<ChapterSample> buildChapterSamples(
            const std::vector<cupuacu::DocumentMarker> &markers,
            const std::uint32_t frameCount)
        {
            std::vector<cupuacu::DocumentMarker> sortedMarkers = markers;
            std::sort(sortedMarkers.begin(), sortedMarkers.end(),
                      [](const cupuacu::DocumentMarker &left,
                         const cupuacu::DocumentMarker &right)
                      {
                          if (left.frame != right.frame)
                          {
                              return left.frame < right.frame;
                          }
                          return left.id < right.id;
                      });

            std::vector<cupuacu::DocumentMarker> usableMarkers;
            usableMarkers.reserve(sortedMarkers.size());
            for (const auto &marker : sortedMarkers)
            {
                if (marker.frame < 0 ||
                    marker.frame >= static_cast<std::int64_t>(frameCount))
                {
                    continue;
                }
                if (!usableMarkers.empty() &&
                    usableMarkers.back().frame == marker.frame)
                {
                    continue;
                }
                usableMarkers.push_back(marker);
            }

            std::vector<ChapterSample> samples;
            samples.reserve(usableMarkers.size());
            for (std::size_t i = 0; i < usableMarkers.size(); ++i)
            {
                const auto startFrame =
                    static_cast<std::uint32_t>(usableMarkers[i].frame);
                const auto endFrame =
                    i + 1 < usableMarkers.size()
                        ? static_cast<std::uint32_t>(usableMarkers[i + 1].frame)
                        : frameCount;
                if (endFrame <= startFrame)
                {
                    continue;
                }

                const auto labelSize = std::min<std::size_t>(
                    usableMarkers[i].label.size(),
                    std::numeric_limits<std::uint16_t>::max());
                Bytes sample;
                appendBe16(sample, static_cast<std::uint16_t>(labelSize));
                sample.insert(sample.end(), usableMarkers[i].label.begin(),
                              usableMarkers[i].label.begin() +
                                  static_cast<std::ptrdiff_t>(labelSize));
                samples.push_back(ChapterSample{
                    .frame = startFrame,
                    .duration = endFrame - startFrame,
                    .bytes = std::move(sample),
                });
            }
            return samples;
        }
    } // namespace

    void appendU8(Bytes &bytes, const std::uint8_t value)
    {
        bytes.push_back(value);
    }

    void appendBe16(Bytes &bytes, const std::uint16_t value)
    {
        appendU8(bytes, static_cast<std::uint8_t>((value >> 8) & 0xffu));
        appendU8(bytes, static_cast<std::uint8_t>(value & 0xffu));
    }

    void appendBe24(Bytes &bytes, const std::uint32_t value)
    {
        if (value > 0x00ffffffu)
        {
            throw std::out_of_range("MP4 24-bit integer overflow");
        }
        appendU8(bytes, static_cast<std::uint8_t>((value >> 16) & 0xffu));
        appendU8(bytes, static_cast<std::uint8_t>((value >> 8) & 0xffu));
        appendU8(bytes, static_cast<std::uint8_t>(value & 0xffu));
    }

    void appendBe32(Bytes &bytes, const std::uint32_t value)
    {
        appendU8(bytes, static_cast<std::uint8_t>((value >> 24) & 0xffu));
        appendU8(bytes, static_cast<std::uint8_t>((value >> 16) & 0xffu));
        appendU8(bytes, static_cast<std::uint8_t>((value >> 8) & 0xffu));
        appendU8(bytes, static_cast<std::uint8_t>(value & 0xffu));
    }

    void appendBe64(Bytes &bytes, const std::uint64_t value)
    {
        appendBe32(bytes, static_cast<std::uint32_t>((value >> 32) & 0xffffffffu));
        appendBe32(bytes, static_cast<std::uint32_t>(value & 0xffffffffu));
    }

    void appendFourCc(Bytes &bytes, const std::string_view fourCc)
    {
        validateFourCc(fourCc);
        for (const char ch : fourCc)
        {
            appendU8(bytes, static_cast<std::uint8_t>(ch));
        }
    }

    Bytes atom(const std::string_view type, const Bytes &payload)
    {
        validateFourCc(type);
        if (payload.size() >
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() -
                                     8u))
        {
            throw std::out_of_range("MP4 atom too large for 32-bit size");
        }

        Bytes bytes;
        bytes.reserve(8 + payload.size());
        appendBe32(bytes, static_cast<std::uint32_t>(8 + payload.size()));
        appendFourCc(bytes, type);
        appendBytes(bytes, payload);
        return bytes;
    }

    Bytes fullAtom(const std::string_view type,
                   const std::uint8_t version,
                   const std::uint32_t flags,
                   const Bytes &payload)
    {
        Bytes fullPayload;
        fullPayload.reserve(4 + payload.size());
        appendU8(fullPayload, version);
        appendBe24(fullPayload, flags);
        appendBytes(fullPayload, payload);
        return atom(type, fullPayload);
    }

    Bytes containerAtom(const std::string_view type,
                        const std::vector<Bytes> &children)
    {
        Bytes payload;
        for (const auto &child : children)
        {
            appendBytes(payload, child);
        }
        return atom(type, payload);
    }

    Bytes ftypAtom()
    {
        Bytes payload;
        appendFourCc(payload, "M4A ");
        appendBe32(payload, 0);
        appendFourCc(payload, "M4A ");
        appendFourCc(payload, "mp42");
        appendFourCc(payload, "isom");
        return atom("ftyp", payload);
    }

    Bytes mdatAtom(const Bytes &payload)
    {
        return atom("mdat", payload);
    }

    Bytes emptyTimeToSampleAtom()
    {
        Bytes payload;
        appendBe32(payload, 0);
        return fullAtom("stts", 0, 0, payload);
    }

    Bytes emptySampleToChunkAtom()
    {
        Bytes payload;
        appendBe32(payload, 0);
        return fullAtom("stsc", 0, 0, payload);
    }

    Bytes emptySampleSizeAtom()
    {
        Bytes payload;
        appendBe32(payload, 0);
        appendBe32(payload, 0);
        return fullAtom("stsz", 0, 0, payload);
    }

    Bytes emptyChunkOffsetAtom()
    {
        Bytes payload;
        appendBe32(payload, 0);
        return fullAtom("stco", 0, 0, payload);
    }

    Bytes timeToSampleAtom(const std::uint32_t frameCount,
                           const std::uint32_t framesPerPacket)
    {
        if (framesPerPacket == 0)
        {
            throw std::invalid_argument("ALAC frames-per-packet must be nonzero");
        }
        if (frameCount == 0)
        {
            return emptyTimeToSampleAtom();
        }

        const std::uint32_t fullPacketCount = frameCount / framesPerPacket;
        const std::uint32_t finalPacketFrames = frameCount % framesPerPacket;
        const std::uint32_t entryCount =
            (fullPacketCount > 0 ? 1u : 0u) +
            (finalPacketFrames > 0 ? 1u : 0u);

        Bytes payload;
        appendBe32(payload, entryCount);
        if (fullPacketCount > 0)
        {
            appendBe32(payload, fullPacketCount);
            appendBe32(payload, framesPerPacket);
        }
        if (finalPacketFrames > 0)
        {
            appendBe32(payload, 1);
            appendBe32(payload, finalPacketFrames);
        }
        return fullAtom("stts", 0, 0, payload);
    }

    Bytes sampleToChunkAtom(const std::uint32_t packetCount)
    {
        if (packetCount == 0)
        {
            return emptySampleToChunkAtom();
        }

        Bytes payload;
        appendBe32(payload, 1);
        appendBe32(payload, 1);
        appendBe32(payload, packetCount);
        appendBe32(payload, 1);
        return fullAtom("stsc", 0, 0, payload);
    }

    Bytes sampleSizeAtom(const std::vector<std::uint32_t> &packetSizes)
    {
        if (packetSizes.empty())
        {
            return emptySampleSizeAtom();
        }

        Bytes payload;
        appendBe32(payload, 0);
        appendBe32(payload, static_cast<std::uint32_t>(packetSizes.size()));
        for (const auto packetSize : packetSizes)
        {
            appendBe32(payload, packetSize);
        }
        return fullAtom("stsz", 0, 0, payload);
    }

    Bytes chunkOffsetAtom(const std::vector<std::uint32_t> &chunkOffsets)
    {
        if (chunkOffsets.empty())
        {
            return emptyChunkOffsetAtom();
        }

        Bytes payload;
        appendBe32(payload, static_cast<std::uint32_t>(chunkOffsets.size()));
        for (const auto chunkOffset : chunkOffsets)
        {
            appendBe32(payload, chunkOffset);
        }
        return fullAtom("stco", 0, 0, payload);
    }

    Bytes alacSampleEntry(const AlacSampleEntryDescription &description)
    {
        if (description.channels == 0 || description.sampleRate == 0 ||
            description.bitDepth == 0 || description.magicCookie.empty())
        {
            throw std::invalid_argument("Invalid ALAC sample entry description");
        }

        Bytes payload;
        payload.resize(6, 0);
        appendBe16(payload, 1);
        appendBe16(payload, 0);
        appendBe16(payload, 0);
        appendBe32(payload, 0);
        appendBe16(payload, description.channels);
        appendBe16(payload, description.bitDepth);
        appendBe16(payload, 0);
        appendBe16(payload, 0);
        // The legacy 16.16 sample-rate field cannot represent rates above
        // 65535 Hz; the ALAC cookie carries the exact source rate.
        const std::uint32_t fixedSampleRate =
            description.sampleRate <= 0xffffu ? description.sampleRate << 16u
                                              : 0u;
        appendBe32(payload, fixedSampleRate);
        appendBytes(payload, fullAtom("alac", 0, 0, description.magicCookie));
        return atom("alac", payload);
    }

    Bytes sampleDescriptionAtom(const std::vector<Bytes> &sampleEntries)
    {
        Bytes payload;
        appendBe32(payload, static_cast<std::uint32_t>(sampleEntries.size()));
        for (const auto &sampleEntry : sampleEntries)
        {
            appendBytes(payload, sampleEntry);
        }
        return fullAtom("stsd", 0, 0, payload);
    }

    Bytes movieHeaderAtom(const std::uint32_t timescale,
                          const std::uint32_t duration,
                          const std::uint32_t nextTrackId)
    {
        if (timescale == 0)
        {
            throw std::invalid_argument("M4A movie timescale must be nonzero");
        }

        Bytes payload;
        appendBe32(payload, 0);
        appendBe32(payload, 0);
        appendBe32(payload, timescale);
        appendBe32(payload, duration);
        appendBe32(payload, 0x00010000u);
        appendBe16(payload, 0x0100u);
        appendBe16(payload, 0);
        appendBe32(payload, 0);
        appendBe32(payload, 0);
        appendBe32(payload, 0x00010000u);
        appendBe32(payload, 0);
        appendBe32(payload, 0);
        appendBe32(payload, 0);
        appendBe32(payload, 0x00010000u);
        appendBe32(payload, 0);
        appendBe32(payload, 0);
        appendBe32(payload, 0);
        appendBe32(payload, 0x40000000u);
        for (int i = 0; i < 6; ++i)
        {
            appendBe32(payload, 0);
        }
        appendBe32(payload, nextTrackId);
        return fullAtom("mvhd", 0, 0, payload);
    }

    Bytes trackHeaderAtom(const std::uint32_t trackId,
                          const std::uint32_t duration,
                          const bool enabled)
    {
        if (trackId == 0)
        {
            throw std::invalid_argument("M4A track id must be nonzero");
        }

        Bytes payload;
        appendBe32(payload, 0);
        appendBe32(payload, 0);
        appendBe32(payload, trackId);
        appendBe32(payload, 0);
        appendBe32(payload, duration);
        appendBe32(payload, 0);
        appendBe32(payload, 0);
        appendBe16(payload, 0);
        appendBe16(payload, 0);
        appendBe16(payload, 0x0100u);
        appendBe16(payload, 0);
        appendBe32(payload, 0x00010000u);
        appendBe32(payload, 0);
        appendBe32(payload, 0);
        appendBe32(payload, 0);
        appendBe32(payload, 0x00010000u);
        appendBe32(payload, 0);
        appendBe32(payload, 0);
        appendBe32(payload, 0);
        appendBe32(payload, 0x40000000u);
        appendBe32(payload, 0);
        appendBe32(payload, 0);
        return fullAtom("tkhd", 0, enabled ? 0x000007u : 0x000000u, payload);
    }

    Bytes mediaHeaderAtom(const std::uint32_t timescale,
                          const std::uint32_t duration)
    {
        if (timescale == 0)
        {
            throw std::invalid_argument("M4A media timescale must be nonzero");
        }

        Bytes payload;
        appendBe32(payload, 0);
        appendBe32(payload, 0);
        appendBe32(payload, timescale);
        appendBe32(payload, duration);
        appendBe16(payload, 0);
        appendBe16(payload, 0);
        return fullAtom("mdhd", 0, 0, payload);
    }

    Bytes soundMediaHeaderAtom()
    {
        Bytes payload;
        appendBe16(payload, 0);
        appendBe16(payload, 0);
        return fullAtom("smhd", 0, 0, payload);
    }

    Bytes handlerReferenceAtom(const std::string_view handlerType)
    {
        Bytes payload;
        appendBe32(payload, 0);
        appendFourCc(payload, handlerType);
        appendBe32(payload, 0);
        appendBe32(payload, 0);
        appendBe32(payload, 0);
        appendU8(payload, 0);
        return fullAtom("hdlr", 0, 0, payload);
    }

    Bytes dataInformationAtom()
    {
        Bytes url = fullAtom("url ", 0, 1);
        Bytes drefPayload;
        appendBe32(drefPayload, 1);
        appendBytes(drefPayload, url);
        return containerAtom("dinf", {fullAtom("dref", 0, 0, drefPayload)});
    }

    Bytes trackReferenceAtom(const std::uint32_t chapterTrackId)
    {
        Bytes chapterReference;
        appendBe32(chapterReference, chapterTrackId);
        return containerAtom("tref", {atom("chap", chapterReference)});
    }

    Bytes editListAtom(const std::uint32_t emptyDuration,
                       const std::uint32_t mediaDuration)
    {
        Bytes payload;
        const bool hasEmptyEdit = emptyDuration != 0;
        appendBe32(payload, hasEmptyEdit ? 2u : 1u);
        if (hasEmptyEdit)
        {
            appendBe32(payload, emptyDuration);
            appendBe32(payload, 0xffffffffu);
            appendBe32(payload, 0x00010000u);
        }
        appendBe32(payload, mediaDuration);
        appendBe32(payload, 0);
        appendBe32(payload, 0x00010000u);
        return containerAtom("edts", {fullAtom("elst", 0, 0, payload)});
    }

    Bytes textMediaHeaderAtom()
    {
        return fullAtom("nmhd", 0, 0);
    }

    Bytes textSampleEntry()
    {
        Bytes payload;
        payload.resize(6, 0);
        appendBe16(payload, 1);
        return atom("text", payload);
    }

    Bytes explicitTimeToSampleAtom(
        const std::vector<std::uint32_t> &sampleDurations)
    {
        if (sampleDurations.empty())
        {
            return emptyTimeToSampleAtom();
        }

        Bytes payload;
        appendBe32(payload, static_cast<std::uint32_t>(sampleDurations.size()));
        for (const auto duration : sampleDurations)
        {
            appendBe32(payload, 1);
            appendBe32(payload, duration);
        }
        return fullAtom("stts", 0, 0, payload);
    }

    Bytes sampleTableAtom(const AlacMovieDescription &description)
    {
        if (description.packetSizes.empty() && description.frameCount != 0)
        {
            throw std::invalid_argument("M4A sample table requires packet sizes");
        }

        const auto entry = alacSampleEntry(description.sampleEntry);
        return containerAtom(
            "stbl",
            {sampleDescriptionAtom({entry}),
             timeToSampleAtom(description.frameCount,
                              description.framesPerPacket),
             sampleToChunkAtom(
                 static_cast<std::uint32_t>(description.packetSizes.size())),
             sampleSizeAtom(description.packetSizes),
             chunkOffsetAtom(description.packetSizes.empty()
                                 ? std::vector<std::uint32_t>{}
                                 : std::vector<std::uint32_t>{
                                       description.mdatPayloadOffset})});
    }

    Bytes mediaInformationAtom(const AlacMovieDescription &description)
    {
        return containerAtom("minf", {soundMediaHeaderAtom(),
                                      dataInformationAtom(),
                                      sampleTableAtom(description)});
    }

    Bytes mediaAtom(const AlacMovieDescription &description)
    {
        return containerAtom("mdia", {mediaHeaderAtom(description.sampleRate,
                                                      description.frameCount),
                                      handlerReferenceAtom("soun"),
                                      mediaInformationAtom(description)});
    }

    Bytes trackAtom(const AlacMovieDescription &description)
    {
        std::vector<Bytes> children{
            trackHeaderAtom(1, description.frameCount, true)};
        if (!description.chapterSampleSizes.empty())
        {
            children.push_back(trackReferenceAtom(2));
        }
        children.push_back(mediaAtom(description));
        return containerAtom("trak", children);
    }

    Bytes chapterSampleTableAtom(const AlacMovieDescription &description)
    {
        return containerAtom(
            "stbl",
            {sampleDescriptionAtom({textSampleEntry()}),
             explicitTimeToSampleAtom(description.chapterSampleDurations),
             sampleToChunkAtom(static_cast<std::uint32_t>(
                 description.chapterSampleSizes.size())),
             sampleSizeAtom(description.chapterSampleSizes),
             chunkOffsetAtom(description.chapterSampleSizes.empty()
                                 ? std::vector<std::uint32_t>{}
                                 : std::vector<std::uint32_t>{
                                       description.chapterMdatPayloadOffset})});
    }

    Bytes chapterMediaInformationAtom(const AlacMovieDescription &description)
    {
        return containerAtom("minf", {textMediaHeaderAtom(),
                                      dataInformationAtom(),
                                      chapterSampleTableAtom(description)});
    }

    Bytes chapterMediaAtom(const AlacMovieDescription &description)
    {
        return containerAtom(
            "mdia", {mediaHeaderAtom(description.sampleRate,
                                     description.chapterMediaDuration),
                     handlerReferenceAtom("text"),
                     chapterMediaInformationAtom(description)});
    }

    Bytes chapterTrackAtom(const AlacMovieDescription &description)
    {
        std::vector<Bytes> children{
            trackHeaderAtom(2, description.frameCount, false)};
        if (description.chapterStartOffset != 0)
        {
            children.push_back(editListAtom(description.chapterStartOffset,
                                            description.chapterMediaDuration));
        }
        children.push_back(chapterMediaAtom(description));
        return containerAtom("trak", children);
    }

    Bytes movieAtom(const AlacMovieDescription &description)
    {
        if (description.sampleRate == 0 || description.framesPerPacket == 0)
        {
            throw std::invalid_argument("Invalid M4A movie description");
        }

        std::vector<Bytes> children{
            movieHeaderAtom(description.sampleRate, description.frameCount,
                            description.chapterSampleSizes.empty() ? 2u : 3u),
            trackAtom(description),
        };
        if (!description.chapterSampleSizes.empty())
        {
            children.push_back(chapterTrackAtom(description));
        }
        return containerAtom("moov", children);
    }

    Bytes assembleAlacM4a(
        const cupuacu::file::alac::AlacEncodedPackets &packets,
        const std::vector<cupuacu::DocumentMarker> &markers)
    {
        if (packets.cookie.bytes.empty() || packets.packetSizes.empty() ||
            packets.bytes.empty() || packets.frameCount == 0 ||
            packets.framesPerPacket == 0)
        {
            throw std::invalid_argument("Invalid ALAC packets for M4A assembly");
        }

        const auto ftyp = ftypAtom();
        const auto chapterSamples =
            buildChapterSamples(markers, packets.frameCount);
        Bytes mdatPayload = packets.bytes;
        std::vector<std::uint32_t> chapterSampleSizes;
        std::vector<std::uint32_t> chapterSampleDurations;
        std::uint32_t chapterStartOffset = 0;
        std::uint32_t chapterMediaDuration = 0;
        chapterSampleSizes.reserve(chapterSamples.size());
        chapterSampleDurations.reserve(chapterSamples.size());
        if (!chapterSamples.empty())
        {
            chapterStartOffset = chapterSamples.front().frame;
        }
        for (const auto &chapter : chapterSamples)
        {
            chapterSampleSizes.push_back(
                static_cast<std::uint32_t>(chapter.bytes.size()));
            chapterSampleDurations.push_back(chapter.duration);
            chapterMediaDuration += chapter.duration;
            appendBytes(mdatPayload, chapter.bytes);
        }
        const auto mdat = mdatAtom(mdatPayload);
        const auto mdatPayloadOffset = static_cast<std::uint32_t>(ftyp.size() + 8);
        const auto chapterMdatPayloadOffset = static_cast<std::uint32_t>(
            mdatPayloadOffset + packets.bytes.size());
        const AlacMovieDescription description{
            .sampleRate = packets.cookie.sampleRate,
            .frameCount = packets.frameCount,
            .framesPerPacket = packets.framesPerPacket,
            .packetSizes = packets.packetSizes,
            .mdatPayloadOffset = mdatPayloadOffset,
            .chapterSampleSizes = chapterSampleSizes,
            .chapterSampleDurations = chapterSampleDurations,
            .chapterMdatPayloadOffset = chapterMdatPayloadOffset,
            .chapterStartOffset = chapterStartOffset,
            .chapterMediaDuration = chapterMediaDuration,
            .sampleEntry =
                {
                    .channels = packets.cookie.channels,
                    .bitDepth = packets.cookie.bitDepth,
                    .sampleRate = packets.cookie.sampleRate,
                    .magicCookie = packets.cookie.bytes,
                },
        };
        const auto moov = movieAtom(description);

        Bytes file;
        file.reserve(ftyp.size() + mdat.size() + moov.size());
        appendBytes(file, ftyp);
        appendBytes(file, mdat);
        appendBytes(file, moov);
        return file;
    }
} // namespace cupuacu::file::m4a
