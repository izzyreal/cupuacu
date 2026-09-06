#include "RevisionPreservationWriter.hpp"
#include "FileIo.hpp"
#include "SampleQuantization.hpp"
#include "wav/WavParser.hpp"
#include "wav/WavMarkerMetadata.hpp"
#include "aiff/AiffParser.hpp"
#include "aiff/AiffMarkerMetadata.hpp"
#include <array>
#include <bit>
#include <fstream>
#include <list>
#include <sndfile.h>

namespace cupuacu::file
{
    namespace
    {
        constexpr std::size_t scratchBytes = 65536;
        struct PcmLayout
        {
            bool wav = false;
            int channels = 0, rate = 0, width = 0;
            SampleFormat format = SampleFormat::Unknown;
            uint64_t offset = 0, bytes = 0;
        };
        int byteWidth(SampleFormat format)
        {
            switch (format)
            {
                case SampleFormat::PCM_S8:
                    return 1;
                case SampleFormat::PCM_S16:
                    return 2;
                case SampleFormat::PCM_S24:
                    return 3;
                case SampleFormat::PCM_S32:
                case SampleFormat::FLOAT32:
                    return 4;
                default:
                    throw std::runtime_error(
                        "Unsupported preserving PCM format");
            }
        }
        PcmLayout layout(const wav::ParsedFile &p)
        {
            auto chunk = p.findChunk("data");
            if (!p.isWave || p.fmtChunkCount != 1 || p.dataChunkCount != 1 ||
                !chunk)
            {
                throw std::runtime_error(
                    "Preservation requires one WAV fmt and data chunk");
            }
            return {true,
                    p.channelCount,
                    p.sampleRate,
                    byteWidth(p.sampleFormat),
                    p.sampleFormat,
                    chunk->payloadOffset,
                    chunk->payloadSize};
        }
        PcmLayout layout(const aiff::ParsedFile &p)
        {
            if ((!p.isAiff && !p.isAifc) || p.commChunkCount != 1 ||
                p.ssndChunkCount != 1)
            {
                throw std::runtime_error(
                    "Preservation requires one AIFF COMM and SSND chunk");
            }
            return {false,          p.channelCount,
                    p.sampleRate,   byteWidth(p.sampleFormat),
                    p.sampleFormat, p.soundDataOffset,
                    p.soundDataSize};
        }
        PcmLayout readLayout(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary);
            std::array<char, 4> magic{};
            if (!input.read(magic.data(), magic.size()))
            {
                throw std::runtime_error("Cannot read preserved source header");
            }
            if (magic == std::array<char, 4>{'R', 'I', 'F', 'F'})
            {
                auto parsed = wav::WavParser::parseFile(path);
                return parsed.sampleFormat == SampleFormat::Unknown
                           ? PcmLayout{}
                           : layout(parsed);
            }
            if (magic == std::array<char, 4>{'F', 'O', 'R', 'M'})
            {
                auto parsed = aiff::AiffParser::parseFile(path);
                return parsed.sampleFormat == SampleFormat::Unknown
                           ? PcmLayout{}
                           : layout(parsed);
            }
            return {}; // Compressed sources require ordinary float encoding.
        }
        void validate(const storage::AudioShape &shape, const PcmLayout &p,
                      const AudioExportSettings &settings)
        {
            int subtype = 0;
            // The editor retains its original sample representation across
            // Save As. The owned container determines the output encoding.
            switch (p.format)
            {
                case SampleFormat::PCM_S8:
                    subtype = p.wav ? SF_FORMAT_PCM_U8 : SF_FORMAT_PCM_S8;
                    break;
                case SampleFormat::PCM_S16:
                    subtype = SF_FORMAT_PCM_16;
                    break;
                case SampleFormat::PCM_S24:
                    subtype = SF_FORMAT_PCM_24;
                    break;
                case SampleFormat::PCM_S32:
                    subtype = SF_FORMAT_PCM_32;
                    break;
                case SampleFormat::FLOAT32:
                    subtype = SF_FORMAT_FLOAT;
                    break;
                default:
                    break;
            }
            if (shape.frames < 0 || shape.channels <= 0 ||
                shape.channels != p.channels || shape.sampleRate != p.rate ||
                settings.majorFormat !=
                    (p.wav ? SF_FORMAT_WAV : SF_FORMAT_AIFF) ||
                !subtype || settings.subtype != subtype)
            {
                throw std::runtime_error(
                    "Preserving output must match the reference PCM format");
            }
        }
        void write32(std::ostream &out, uint64_t value, bool little)
        {
            if (value > UINT32_MAX)
            {
                throw std::length_error(
                    "Preserving RIFF/AIFF output exceeds its 32-bit container "
                    "limit");
            }
            std::array<char, 4> bytes;
            for (int i = 0; i < 4; ++i)
            {
                bytes[little ? i : 3 - i] = char(value >> (8 * i));
            }
            out.write(bytes.data(), bytes.size());
        }
        void copy(std::istream &in, std::ostream &out, uint64_t offset,
                  uint64_t bytes, const std::function<void()> &check)
        {
            if (offset > INT64_MAX || bytes > INT64_MAX - offset)
            {
                throw std::length_error("Preserved byte range overflow");
            }
            in.clear();
            in.seekg(std::streamoff(offset));
            auto memory = storage::reserveWorking(scratchBytes,
                                                  storage::MemoryUse::Export);
            std::array<char, scratchBytes> buffer;
            while (bytes)
            {
                check();
                const auto n = std::min<uint64_t>(bytes, buffer.size());
                if (!in.read(buffer.data(), n) || !out.write(buffer.data(), n))
                {
                    throw std::runtime_error(
                        "Cannot copy preserved file bytes");
                }
                bytes -= n;
            }
        }
        struct RawSource
        {
            std::filesystem::path path;
            PcmLayout layout;
            std::ifstream input;
        };
        // At most eight source descriptors. Raw and output buffers are each
        // 64 KiB, regardless of recording length or number of pasted sources.
        class SampleWriter
        {
            const storage::AudioEditRevision &audio;
            PcmLayout target;
            std::list<RawSource> sources;
            RawSource *rawOwner = nullptr;
            uint64_t rawOffset = 0, rawLength = 0;
            std::shared_ptr<void> memory = storage::reserveWorking(
                3 * scratchBytes, storage::MemoryUse::Export);
            std::array<char, scratchBytes> raw{}, encoded{};
            std::array<float, scratchBytes / sizeof(float)> floats{};
            const WriteProgressCallback &progress;
            std::string detail;
            RawSource &source(const std::filesystem::path &path)
            {
                auto found = std::find_if(sources.begin(), sources.end(),
                                          [&](const auto &s)
                                          {
                                              return s.path == path;
                                          });
                if (found != sources.end())
                {
                    sources.splice(sources.begin(), sources, found);
                }
                else
                {
                    if (sources.size() == 8)
                    {
                        if (rawOwner == &sources.back())
                        {
                            rawOwner = nullptr;
                        }
                        sources.pop_back();
                    }
                    auto format = readLayout(path);
                    sources.push_front(
                        {path, format, std::ifstream(path, std::ios::binary)});
                    if (!sources.front().input)
                    {
                        throw std::runtime_error(
                            "Cannot open preserved source");
                    }
                }
                return sources.front();
            }
            void encode(float value, char *out)
            {
                uint32_t bits = target.format == SampleFormat::FLOAT32
                                    ? std::bit_cast<uint32_t>(value)
                                    : uint32_t(quantizeIntegerPcmSample(
                                          target.format, value, false));
                if (target.wav && target.width == 1)
                {
                    bits += 128;
                }
                for (int i = 0; i < target.width; ++i)
                {
                    out[target.wav ? i : target.width - 1 - i] =
                        char(bits >> (8 * i));
                }
            }

        public:
            SampleWriter(const storage::AudioEditRevision &audio,
                         PcmLayout target,
                         const WriteProgressCallback &progress,
                         std::string detail)
                : audio(audio), target(target), progress(progress),
                  detail(std::move(detail))
            {
            }
            void write(std::ostream &out)
            {
                const auto shape = audio.shape();
                const uint64_t stride = uint64_t(target.width) * shape.channels;
                if (stride > encoded.size())
                {
                    throw std::length_error(
                        "PCM channel frame exceeds scratch budget");
                }
                const int64_t chunk =
                    std::min<uint64_t>(encoded.size() / stride, floats.size());
                for (int64_t first = 0; first < shape.frames; first += chunk)
                {
                    if (progress)
                    {
                        progress(detail,
                                 .05 + .9 * double(first) / shape.frames);
                    }
                    const int64_t count = std::min(chunk, shape.frames - first);
                    for (int c = 0; c < shape.channels; ++c)
                    {
                        int64_t destination = 0;
                        audio.visitSourceRanges(
                            c, first, count,
                            [&](const auto &range)
                            {
                                RawSource *original = nullptr;
                                if (range.source &&
                                    !range.source->sourcePath().empty())
                                {
                                    auto &s =
                                        source(range.source->sourcePath());
                                    if (s.layout.format == target.format)
                                    {
                                        original = &s;
                                    }
                                }
                                if (original)
                                {
                                    const auto &p = original->layout;
                                    const uint64_t sourceStride =
                                        uint64_t(p.width) * p.channels;
                                    if (!sourceStride ||
                                        sourceStride > raw.size() ||
                                        range.channel < 0 ||
                                        range.channel >= p.channels ||
                                        uint64_t(range.start) >
                                            p.bytes / sourceStride ||
                                        uint64_t(range.frames) >
                                            p.bytes / sourceStride -
                                                range.start)
                                    {
                                        throw std::runtime_error(
                                            "Preserved sample range is outside "
                                            "source data");
                                    }
                                    for (int64_t pos = 0; pos < range.frames;)
                                    {
                                        const auto n = std::min<int64_t>(
                                            raw.size() / sourceStride,
                                            range.frames - pos);
                                        auto &in = original->input;
                                        const uint64_t offset =
                                            p.offset +
                                            (range.start + pos) * sourceStride;
                                        const uint64_t length =
                                            n * sourceStride;
                                        if (rawOwner != original ||
                                            offset < rawOffset ||
                                            offset - rawOffset > rawLength ||
                                            length > rawLength -
                                                         (offset - rawOffset))
                                        {
                                            in.clear();
                                            in.seekg(std::streamoff(offset));
                                            if (!in.read(raw.data(), length))
                                            {
                                                throw std::runtime_error(
                                                    "Cannot read preserved PCM "
                                                    "range");
                                            }
                                            rawOwner = original;
                                            rawOffset = offset;
                                            rawLength = length;
                                        }
                                        for (int64_t i = 0; i < n; ++i)
                                        {
                                            auto src = raw.data() +
                                                       (offset - rawOffset) +
                                                       i * sourceStride +
                                                       range.channel * p.width;
                                            auto dst = encoded.data() +
                                                       (destination + pos + i) *
                                                           stride +
                                                       c * target.width;
                                            for (int b = 0; b < p.width; ++b)
                                            {
                                                dst[b] =
                                                    src[p.wav == target.wav
                                                            ? b
                                                            : p.width - 1 - b];
                                            }
                                            if (p.width == 1 &&
                                                p.wav != target.wav)
                                            {
                                                dst[0] =
                                                    char(uint8_t(dst[0]) ^ 128);
                                            }
                                        }
                                        pos += n;
                                    }
                                }
                                else
                                {
                                    auto values =
                                        std::span(floats).first(range.frames);
                                    if (range.source)
                                    {
                                        range.source->readChannel(
                                            range.channel, range.start, values);
                                    }
                                    else
                                    {
                                        std::fill(values.begin(), values.end(),
                                                  range.constantValue);
                                    }
                                    for (int64_t i = 0; i < range.frames; ++i)
                                    {
                                        encode(values[i],
                                               encoded.data() +
                                                   (destination + i) * stride +
                                                   c * target.width);
                                    }
                                }
                                destination += range.frames;
                            });
                    }
                    if (!out.write(encoded.data(), count * stride))
                    {
                        throw std::runtime_error(
                            "Cannot write preserving PCM output");
                    }
                }
            }
        };
    } // namespace

    std::filesystem::path
    revisionPreservationReference(const DocumentSession &session)
    {
        if (session.preservationSource)
        {
            return session.preservationSource->sourcePath();
        }
        return session.preservationReferenceFile.empty()
                   ? session.currentFile
                   : session.preservationReferenceFile;
    }
    OverwritePreservationState
    assessRevisionPreservation(const DocumentSession &session,
                               const AudioExportSettings &settings)
    {
        try
        {
            const auto reference = revisionPreservationReference(session);
            validate(session.getAudioReader()->shape(), readLayout(reference),
                     settings);
            return {.available = true};
        }
        catch (const std::exception &error)
        {
            return {.available = false, .reason = error.what()};
        }
    }
    void writePreservingRevision(const storage::AudioEditRevision &audio,
                                 const std::vector<DocumentMarker> &markers,
                                 const std::filesystem::path &reference,
                                 const std::filesystem::path &outputPath,
                                 const AudioExportSettings &settings,
                                 const WriteProgressCallback &progress)
    {
        const auto target = readLayout(reference);
        validate(audio.shape(), target, settings);
        const auto shape = audio.shape();
        const uint64_t stride = uint64_t(target.width) * shape.channels;
        if (uint64_t(shape.frames) > UINT32_MAX / stride)
        {
            throw std::length_error(
                "Preserving RIFF/AIFF audio exceeds its 32-bit container "
                "limit");
        }
        const uint64_t bytes = uint64_t(shape.frames) * stride;
        auto check = [&]
        {
            if (progress)
            {
                progress(outputPath.string(), std::nullopt);
            }
        };
        check();
        writeFileAtomically(
            outputPath,
            [&](const auto &temporary)
            {
                std::ifstream input(reference, std::ios::binary);
                std::ofstream output(temporary,
                                     std::ios::binary | std::ios::trunc);
                if (!input || !output)
                {
                    throw std::runtime_error("Cannot open preserving file");
                }
                SampleWriter samples(audio, target, progress,
                                     outputPath.string());
                copy(input, output, 0, 12, check);
                if (target.wav)
                {
                    const auto parsed = wav::WavParser::parseFile(reference);
                    bool wroteMarkers = false;
                    for (const auto &chunk : parsed.chunks)
                    {
                        if (wav::markers::isMarkerChunk(input, chunk))
                        {
                            continue;
                        }
                        if (std::memcmp(chunk.id, "data", 4) == 0)
                        {
                            wav::markers::writeMarkerChunks(output, markers);
                            wroteMarkers = true;
                            output.write("data", 4);
                            write32(output, bytes, true);
                            samples.write(output);
                            if (bytes & 1)
                            {
                                output.put(0);
                            }
                        }
                        else if (std::memcmp(chunk.id, "fact", 4) == 0 &&
                                 chunk.payloadSize >= 4)
                        {
                            const auto at = output.tellp();
                            copy(input, output, chunk.headerOffset,
                                 8 + uint64_t(chunk.paddedPayloadSize), check);
                            const auto end = output.tellp();
                            output.seekp(at + std::streamoff(8));
                            write32(output, shape.frames, true);
                            output.seekp(end);
                        }
                        else
                        {
                            copy(input, output, chunk.headerOffset,
                                 8 + uint64_t(chunk.paddedPayloadSize), check);
                        }
                    }
                    if (!wroteMarkers)
                    {
                        throw std::runtime_error("Missing WAV data chunk");
                    }
                }
                else
                {
                    const auto parsed = aiff::AiffParser::parseFile(reference);
                    for (const auto &chunk : parsed.chunks)
                    {
                        if (std::memcmp(chunk.id, "MARK", 4) == 0)
                        {
                            continue;
                        }
                        if (std::memcmp(chunk.id, "SSND", 4) == 0)
                        {
                            aiff::markers::writeMarkerChunks(output, markers);
                            output.write("SSND", 4);
                            write32(output,
                                    8 + uint64_t(parsed.ssndOffset) + bytes,
                                    false);
                            write32(output, parsed.ssndOffset, false);
                            write32(output, parsed.ssndBlockSize, false);
                            copy(input, output, chunk.payloadOffset + 8,
                                 parsed.ssndOffset, check);
                            samples.write(output);
                            if ((bytes + parsed.ssndOffset) & 1)
                            {
                                output.put(0);
                            }
                        }
                        else if (std::memcmp(chunk.id, "COMM", 4) == 0)
                        {
                            const auto at = output.tellp();
                            copy(input, output, chunk.headerOffset,
                                 8 + uint64_t(chunk.paddedPayloadSize), check);
                            const auto end = output.tellp();
                            output.seekp(at +
                                         std::streamoff(
                                             parsed.commSampleFrameCountOffset -
                                             chunk.headerOffset));
                            write32(output, shape.frames, false);
                            output.seekp(end);
                        }
                        else
                        {
                            copy(input, output, chunk.headerOffset,
                                 8 + uint64_t(chunk.paddedPayloadSize), check);
                        }
                    }
                }
                const auto end = output.tellp();
                if (end < std::streamoff(12))
                {
                    throw std::runtime_error(
                        "Invalid preserving output length");
                }
                output.seekp(4);
                write32(output, uint64_t(end) - 8, target.wav);
                output.seekp(end);
                if (progress)
                {
                    progress(outputPath.string(),
                             1.0); // Cancellation before replacement.
                }
                output.flush();
                if (!output)
                {
                    throw std::runtime_error("Cannot flush preserving output");
                }
                output.close();
                if (!output)
                {
                    throw std::runtime_error("Cannot close preserving output");
                }
            });
    }
} // namespace cupuacu::file
