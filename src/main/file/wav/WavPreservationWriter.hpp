#pragma once

#include "../../State.hpp"
#include "../ChunkedPreservationRewrite.hpp"
#include "../PcmPreservationIO.hpp"
#include "WavMarkerMetadata.hpp"
#include "WavParser.hpp"
#include "WavPreservationSupport.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace cupuacu::file::wav
{
    class WavPreservationWriter
    {
    private:
        static std::size_t sampleByteWidth(const cupuacu::SampleFormat format)
        {
            switch (format)
            {
                case cupuacu::SampleFormat::PCM_S8:
                    return 1;
                case cupuacu::SampleFormat::PCM_S16:
                    return sizeof(std::int16_t);
                case cupuacu::SampleFormat::FLOAT32:
                    return sizeof(float);
                default:
                    throw std::runtime_error(
                        "Unsupported preserving WAV sample format");
            }
        }

        static std::uint32_t expectedDataSizeBytes(
            const cupuacu::Document::ReadLease &document)
        {
            return cupuacu::file::preservation::
                expectedInterleavedDataSizeBytes(
                    document, sampleByteWidth(document.getSampleFormat()));
        }

        static bool canPatchDataChunkInPlace(
            const cupuacu::Document::ReadLease &document,
            const ParsedFile &parsed)
        {
            if ((parsed.sampleFormat != cupuacu::SampleFormat::PCM_S8 &&
                 parsed.sampleFormat != cupuacu::SampleFormat::PCM_S16 &&
                 parsed.sampleFormat != cupuacu::SampleFormat::FLOAT32) ||
                parsed.fmtChunkCount != 1 ||
                parsed.dataChunkCount != 1)
            {
                return false;
            }

            if (document.getChannelCount() != parsed.channelCount ||
                document.getSampleRate() != parsed.sampleRate ||
                document.getSampleFormat() != parsed.sampleFormat)
            {
                return false;
            }

            const auto *dataChunk = parsed.findChunk("data");
            if (dataChunk == nullptr)
            {
                return false;
            }

            return dataChunk->payloadSize == expectedDataSizeBytes(document);
        }

        static std::array<char, sizeof(std::int16_t)>
        encodeLe16(const std::int16_t value)
        {
            const auto encoded = static_cast<std::uint16_t>(value);
            return {static_cast<char>(encoded & 0xffu),
                    static_cast<char>((encoded >> 8) & 0xffu)};
        }

        static std::array<char, 1> encodeS8(const std::int64_t value)
        {
            return {static_cast<char>(static_cast<std::int8_t>(value))};
        }

        static std::array<char, 1> encodeWavPcmU8(const std::int64_t value)
        {
            const auto signedValue = static_cast<std::int16_t>(value);
            const auto unsignedValue = static_cast<std::uint8_t>(
                std::clamp<int>(static_cast<int>(signedValue) + 128, 0, 255));
            return {static_cast<char>(unsignedValue)};
        }

        static std::vector<char> encodeSampleBytes(
            const cupuacu::SampleFormat format, const std::int64_t value)
        {
            if (format == cupuacu::SampleFormat::PCM_S8)
            {
                const auto encoded = encodeWavPcmU8(value);
                return std::vector<char>(encoded.begin(), encoded.end());
            }

            const auto encoded = encodeLe16(static_cast<std::int16_t>(value));
            return std::vector<char>(encoded.begin(), encoded.end());
        }

        static std::vector<char> encodeFloat32Le(const float value)
        {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            return {static_cast<char>(bits & 0xffu),
                    static_cast<char>((bits >> 8) & 0xffu),
                    static_cast<char>((bits >> 16) & 0xffu),
                    static_cast<char>((bits >> 24) & 0xffu)};
        }

        static std::vector<char>
        readOriginalSampleBytes(std::istream &input, const ParsedFile &parsed,
                                const std::int64_t channel,
                                const std::int64_t frame)
        {
            const auto *dataChunk = parsed.findChunk("data");
            if (dataChunk == nullptr)
            {
                throw std::runtime_error("data chunk not found");
            }

            const auto sampleIndex =
                frame * static_cast<std::int64_t>(parsed.channelCount) + channel;
            const auto byteOffset = static_cast<std::streamoff>(
                dataChunk->payloadOffset +
                sampleIndex * static_cast<std::int64_t>(
                                  sampleByteWidth(parsed.sampleFormat)));
            std::vector<char> encoded(sampleByteWidth(parsed.sampleFormat));
            input.seekg(byteOffset, std::ios::beg);
            if (!input.read(encoded.data(),
                            static_cast<std::streamsize>(encoded.size())))
            {
                throw std::runtime_error("Failed to read original WAV sample bytes");
            }
            return encoded;
        }

        static void copyOriginalSampleSpan(std::istream &input,
                                           std::ostream &output,
                                           const ParsedFile &parsed,
                                           const std::int64_t startChannel,
                                           const std::int64_t startFrame,
                                           const std::size_t sampleCount)
        {
            if (sampleCount == 0)
            {
                return;
            }

            const auto *dataChunk = parsed.findChunk("data");
            if (dataChunk == nullptr)
            {
                throw std::runtime_error("data chunk not found");
            }

            const auto startSampleIndex =
                startFrame * static_cast<std::int64_t>(parsed.channelCount) +
                startChannel;
            const auto byteOffset = static_cast<std::streamoff>(
                dataChunk->payloadOffset +
                startSampleIndex * static_cast<std::int64_t>(
                                       sampleByteWidth(parsed.sampleFormat)));
            std::size_t remainingBytes =
                sampleCount * sampleByteWidth(parsed.sampleFormat);

            input.clear();
            input.seekg(byteOffset, std::ios::beg);

            std::array<char, 64 * 1024> buffer{};
            while (remainingBytes > 0)
            {
                const auto chunkBytes = std::min<std::size_t>(remainingBytes,
                                                              buffer.size());
                if (!input.read(buffer.data(),
                                static_cast<std::streamsize>(chunkBytes)))
                {
                    throw std::runtime_error(
                        "Failed to read original WAV sample bytes");
                }

                output.write(buffer.data(),
                             static_cast<std::streamsize>(chunkBytes));
                if (!output)
                {
                    throw std::runtime_error("Failed to write WAV sample bytes");
                }
                remainingBytes -= chunkBytes;
            }
        }

        static void patchSamplesInPlace(
            const cupuacu::Document::ReadLease &document, std::fstream &io,
            const ParsedFile &parsed,
            const cupuacu::file::PreservationProgressCallback &progress = {},
            const std::string &progressDetail = {})
        {
            const auto *dataChunk = parsed.findChunk("data");
            if (dataChunk == nullptr)
            {
                throw std::runtime_error("data chunk not found");
            }

            cupuacu::file::preservation::patchDirtySamplesInPlace(
                document, io,
                sampleByteWidth(parsed.sampleFormat),
                static_cast<std::size_t>(parsed.channelCount),
                dataChunk->payloadOffset,
                [&](const float sample)
                {
                    if (parsed.sampleFormat == cupuacu::SampleFormat::FLOAT32)
                    {
                        return encodeFloat32Le(sample);
                    }
                    const auto quantized =
                        quantizeIntegerPcmSample(parsed.sampleFormat, sample, false);
                    return encodeSampleBytes(parsed.sampleFormat, quantized);
                },
                progress, progressDetail);
        }

        static void patchDataChunkInPlace(
            const cupuacu::Document::ReadLease &document,
            const std::filesystem::path &inputPath,
            const cupuacu::file::PreservationProgressCallback &progress = {},
            const std::string &progressDetail = {})
        {
            writeFileAtomically(
                inputPath,
                [&](const std::filesystem::path &outputPath)
                {
                    std::filesystem::copy_file(
                        inputPath, outputPath,
                        std::filesystem::copy_options::overwrite_existing);
                    std::fstream io(outputPath,
                                    std::ios::binary | std::ios::in | std::ios::out);
                    if (!io.is_open())
                    {
                        throw cupuacu::file::detail::makeIoFailure(
                            "Failed to open output file",
                            cupuacu::file::detail::describeErrno(errno));
                    }

                    const ParsedFile parsed = WavParser::parseFile(outputPath);
                    patchSamplesInPlace(document, io, parsed, progress,
                                        progressDetail);
                    io.flush();
                });
        }

        static void copyPrefix(std::istream &input, std::ostream &output,
                               const ParsedFile &parsed)
        {
            const auto *dataChunk = parsed.findChunk("data");
            if (dataChunk == nullptr)
            {
                throw std::runtime_error("data chunk not found");
            }

            cupuacu::file::preservation::copyByteRange(
                input, output, 0, dataChunk->headerOffset,
                "Failed to read bytes before data chunk");
        }

        static void writeDataChunk(
            const cupuacu::Document::ReadLease &document, std::istream &input,
            std::ostream &output, const ParsedFile &parsed,
            const cupuacu::file::PreservationProgressCallback &progress = {},
            const std::string &progressDetail = {})
        {
            output.write("data", 4);
            const std::uint32_t dataSize = expectedDataSizeBytes(document);
            output.write(reinterpret_cast<const char *>(&dataSize), 4);

            const std::size_t channelCount =
                static_cast<std::size_t>(document.getChannelCount());
            const std::size_t frames =
                static_cast<std::size_t>(document.getFrameCount());
            constexpr std::size_t progressStrideFrames = 16384;
            cupuacu::file::preservation::publishPreservationProgress(
                progress, progressDetail, 0, frames, 0.05, 0.95);

            bool haveOriginalSpan = false;
            std::int64_t originalSpanStartChannel = 0;
            std::int64_t originalSpanStartFrame = 0;
            std::int64_t previousOriginalSampleIndex = -1;
            std::size_t originalSpanSampleCount = 0;
            const auto flushOriginalSpan = [&]
            {
                if (!haveOriginalSpan)
                {
                    return;
                }

                copyOriginalSampleSpan(input, output, parsed,
                                       originalSpanStartChannel,
                                       originalSpanStartFrame,
                                       originalSpanSampleCount);
                haveOriginalSpan = false;
                originalSpanSampleCount = 0;
                previousOriginalSampleIndex = -1;
            };

            for (std::size_t frame = 0; frame < frames; ++frame)
            {
                for (std::size_t channel = 0; channel < channelCount; ++channel)
                {
                    const auto channelIndex = static_cast<std::int64_t>(channel);
                    const auto frameIndex = static_cast<std::int64_t>(frame);
                    const auto provenance =
                        document.getSampleProvenance(channelIndex, frameIndex);
                    const bool canCopyOriginal =
                        !document.isDirty(channelIndex, frameIndex) &&
                        provenance.sourceId == document.getPreservationSourceId() &&
                        provenance.frameIndex >= 0;

                    if (canCopyOriginal)
                    {
                        const auto originalSampleIndex =
                            provenance.frameIndex *
                                static_cast<std::int64_t>(channelCount) +
                            channelIndex;
                        if (haveOriginalSpan &&
                            originalSampleIndex ==
                                previousOriginalSampleIndex + 1)
                        {
                            ++originalSpanSampleCount;
                            previousOriginalSampleIndex = originalSampleIndex;
                            continue;
                        }

                        flushOriginalSpan();
                        haveOriginalSpan = true;
                        originalSpanStartChannel = channelIndex;
                        originalSpanStartFrame = provenance.frameIndex;
                        previousOriginalSampleIndex = originalSampleIndex;
                        originalSpanSampleCount = 1;
                        continue;
                    }

                    flushOriginalSpan();

                    const float sample = document.getSample(channelIndex, frameIndex);
                    if (document.getSampleFormat() ==
                        cupuacu::SampleFormat::FLOAT32)
                    {
                        const auto encoded = encodeFloat32Le(sample);
                        output.write(encoded.data(),
                                     static_cast<std::streamsize>(encoded.size()));
                    }
                    else
                    {
                        const auto quantized = quantizeIntegerPcmSample(
                            document.getSampleFormat(), sample, false);
                        const auto encoded =
                            encodeSampleBytes(document.getSampleFormat(),
                                              quantized);
                        output.write(encoded.data(),
                                     static_cast<std::streamsize>(encoded.size()));
                    }

                    if (!output)
                    {
                        throw std::runtime_error("Failed to write WAV sample bytes");
                    }
                }

                if ((frame + 1) % progressStrideFrames == 0 || frame + 1 == frames)
                {
                    cupuacu::file::preservation::publishPreservationProgress(
                        progress, progressDetail, frame + 1, frames, 0.05, 0.95);
                }
            }

            flushOriginalSpan();

            if ((dataSize & 1u) != 0u)
            {
                output.put('\0');
            }
        }

        static void copySuffix(std::istream &input, std::ostream &output,
                               const ParsedFile &parsed)
        {
            const auto *dataChunk = parsed.findChunk("data");
            if (dataChunk == nullptr)
            {
                throw std::runtime_error("data chunk not found");
            }

            cupuacu::file::preservation::copySuffix(
                input, output,
                dataChunk->payloadOffset + dataChunk->paddedPayloadSize,
                "Failed to read bytes after data chunk");
        }

        static void updateRiffSizeField(std::ostream &output,
                                        const ParsedFile &parsed)
        {
            const auto fileSizePosition = output.tellp();
            if (fileSizePosition < 0)
            {
                throw std::runtime_error(
                    "Failed to determine output WAV file size");
            }
            if (fileSizePosition < static_cast<std::streamoff>(8))
            {
                throw std::runtime_error("Output WAV file is too small");
            }

            const auto fileSize = static_cast<std::uint64_t>(fileSizePosition);
            const auto riffDataSize = fileSize - 8;
            if (riffDataSize > std::numeric_limits<std::uint32_t>::max())
            {
                throw std::runtime_error("Output WAV file is too large");
            }

            const auto encodedSize = static_cast<std::uint32_t>(riffDataSize);
            output.seekp(static_cast<std::streamoff>(parsed.riffSizeOffset),
                         std::ios::beg);
            output.write(reinterpret_cast<const char *>(&encodedSize),
                         sizeof(encodedSize));
            output.seekp(fileSizePosition, std::ios::beg);
            if (!output)
            {
                throw std::runtime_error("Failed to update RIFF size field");
            }
        }

        static bool hasMarkerChunks(std::istream &input, const ParsedFile &parsed)
        {
            return std::any_of(parsed.chunks.begin(), parsed.chunks.end(),
                               [&](const ChunkInfo &chunk)
                               { return markers::isMarkerChunk(input, chunk); });
        }

        static void rewriteWholeFileWithMarkers(
            const cupuacu::file::PreservationWriteInput &writeInput,
            const std::filesystem::path &outputPath, const ParsedFile &parsed)
        {
            cupuacu::file::writeFileAtomically(
                outputPath,
                [&](const std::filesystem::path &temporaryPath)
                {
                    auto input = cupuacu::file::preservation::openInputFileStream(
                        writeInput.referencePath);
                    auto output = cupuacu::file::preservation::openOutputFileStream(
                        temporaryPath);

                    cupuacu::file::preservation::copyByteRange(
                        input, output, 0, 12, "Failed to copy WAV header");

                    bool insertedMarkers = false;
                    for (const auto &chunk : parsed.chunks)
                    {
                        if (!insertedMarkers &&
                            (markers::isMarkerChunk(input, chunk) ||
                             std::memcmp(chunk.id, "data", 4) == 0))
                        {
                            markers::writeMarkerChunks(
                                output, writeInput.document.getMarkers());
                            insertedMarkers = true;
                        }

                        if (markers::isMarkerChunk(input, chunk))
                        {
                            continue;
                        }

                        if (std::memcmp(chunk.id, "data", 4) == 0)
                        {
                            writeDataChunk(writeInput.document, input, output,
                                           parsed, writeInput.progress,
                                           writeInput.outputPath.string());
                            continue;
                        }

                        cupuacu::file::preservation::copyByteRange(
                            input, output, chunk.headerOffset,
                            8 + chunk.paddedPayloadSize,
                            "Failed to copy WAV chunk");
                    }

                    if (!insertedMarkers)
                    {
                        markers::writeMarkerChunks(
                            output, writeInput.document.getMarkers());
                    }

                    updateRiffSizeField(output, parsed);
                    output.flush();
                });
        }

    public:
        static void writePreservingWavFile(
            cupuacu::State *state, const std::filesystem::path &referencePath,
            const std::filesystem::path &outputPath,
            const cupuacu::file::AudioExportSettings &settings)
        {
            if (state == nullptr)
            {
                throw std::invalid_argument("State is null");
            }

            const auto support =
                WavPreservationSupport::assessAgainstReference(state, settings);
            if (!support.supported)
            {
                throw std::runtime_error(support.reason);
            }
            const auto lease =
                state->getActiveDocumentSession().document.acquireReadLease();
            writePreservingWavFile(cupuacu::file::PreservationWriteInput{
                .document = lease,
                .referencePath = referencePath,
                .outputPath = outputPath,
                .settings = settings,
            });
        }

        static void writePreservingWavFile(
            const cupuacu::file::PreservationWriteInput &writeInput)
        {
            if (writeInput.progress)
            {
                writeInput.progress(writeInput.outputPath.string(), 0.0);
            }

            const ParsedFile parsed =
                WavParser::parseFile(writeInput.referencePath);

            const bool markersRequireRewrite =
                !writeInput.document.getMarkers().empty();

            if (!markersRequireRewrite)
            {
                if (writeInput.referencePath == writeInput.outputPath &&
                    canPatchDataChunkInPlace(writeInput.document, parsed))
                {
                    patchDataChunkInPlace(writeInput.document,
                                          writeInput.referencePath,
                                          writeInput.progress,
                                          writeInput.outputPath.string());
                    if (writeInput.progress)
                    {
                        writeInput.progress(writeInput.outputPath.string(), 1.0);
                    }
                    return;
                }

                cupuacu::file::preservation::rewriteChunkPreserving(
                    writeInput.referencePath, writeInput.outputPath,
                    [&](std::istream &input, std::ostream &output)
                    {
                        copyPrefix(input, output, parsed);
                    },
                    [&](std::istream &input, std::ostream &output)
                    {
                        writeDataChunk(writeInput.document, input, output,
                                       parsed, writeInput.progress,
                                       writeInput.outputPath.string());
                    },
                    [&](std::istream &input, std::ostream &output)
                    {
                        copySuffix(input, output, parsed);
                    },
                    [&](std::ostream &output)
                    {
                        updateRiffSizeField(output, parsed);
                    });
                if (writeInput.progress)
                {
                    writeInput.progress(writeInput.outputPath.string(), 1.0);
                }
                return;
            }

            rewriteWholeFileWithMarkers(writeInput, writeInput.outputPath,
                                        parsed);
            if (writeInput.progress)
            {
                writeInput.progress(writeInput.outputPath.string(), 1.0);
            }
        }

        static void overwritePreservingWavFile(cupuacu::State *state)
        {
            if (state == nullptr)
            {
                throw std::invalid_argument("State is null");
            }

            const auto &session = state->getActiveDocumentSession();
            auto settings = session.currentFileExportSettings;
            if (!settings.has_value())
            {
                settings = cupuacu::file::defaultExportSettingsForPath(
                    session.currentFile, session.document.getSampleFormat());
            }
            if (!settings.has_value())
            {
                throw std::runtime_error(
                    "Could not determine current file export settings");
            }

            const auto referenceFile = !session.preservationReferenceFile.empty()
                                           ? session.preservationReferenceFile
                                           : session.currentFile;
            writePreservingWavFile(state, referenceFile,
                                   session.currentFile, *settings);
        }
    };
} // namespace cupuacu::file::wav
