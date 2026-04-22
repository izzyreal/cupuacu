#pragma once

#include "../../State.hpp"
#include "../ChunkedPreservationRewrite.hpp"
#include "../PcmPreservationIO.hpp"
#include "AiffMarkerMetadata.hpp"
#include "AiffParser.hpp"
#include "AiffPreservationSupport.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace cupuacu::file::aiff
{
    class AiffPreservationWriter
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
                        "Unsupported preserving AIFF sample format");
            }
        }

        static std::uint32_t expectedSoundDataSizeBytes(const cupuacu::State *state)
        {
            return cupuacu::file::preservation::
                expectedInterleavedDataSizeBytes(
                    state, sampleByteWidth(
                               state->getActiveDocumentSession()
                                   .document.getSampleFormat()));
        }

        static bool canPatchSoundDataInPlace(const cupuacu::State *state,
                                             const ParsedFile &parsed)
        {
            if (state == nullptr ||
                (parsed.sampleFormat != cupuacu::SampleFormat::PCM_S8 &&
                 parsed.sampleFormat != cupuacu::SampleFormat::PCM_S16 &&
                 parsed.sampleFormat != cupuacu::SampleFormat::FLOAT32) ||
                parsed.commChunkCount != 1 ||
                parsed.ssndChunkCount != 1)
            {
                return false;
            }

            const auto &document = state->getActiveDocumentSession().document;
            if (document.getChannelCount() != parsed.channelCount ||
                document.getSampleRate() != parsed.sampleRate ||
                document.getSampleFormat() != parsed.sampleFormat)
            {
                return false;
            }

            const auto *ssndChunk = parsed.findChunk("SSND");
            if (ssndChunk == nullptr)
            {
                return false;
            }

            return parsed.soundDataSize == expectedSoundDataSizeBytes(state);
        }

        static std::array<char, sizeof(std::int16_t)>
        encodeBe16(const std::int16_t value)
        {
            const auto encoded = static_cast<std::uint16_t>(value);
            return {static_cast<char>((encoded >> 8) & 0xffu),
                    static_cast<char>(encoded & 0xffu)};
        }

        static std::array<char, 1> encodeS8(const std::int64_t value)
        {
            return {static_cast<char>(static_cast<std::int8_t>(value))};
        }

        static std::vector<char> encodeSampleBytes(
            const cupuacu::SampleFormat format, const std::int64_t value)
        {
            if (format == cupuacu::SampleFormat::PCM_S8)
            {
                const auto encoded = encodeS8(value);
                return std::vector<char>(encoded.begin(), encoded.end());
            }

            const auto encoded = encodeBe16(static_cast<std::int16_t>(value));
            return std::vector<char>(encoded.begin(), encoded.end());
        }

        static std::vector<char> encodeFloat32Be(const float value)
        {
            const auto bits = std::bit_cast<std::uint32_t>(value);
            return {static_cast<char>((bits >> 24) & 0xffu),
                    static_cast<char>((bits >> 16) & 0xffu),
                    static_cast<char>((bits >> 8) & 0xffu),
                    static_cast<char>(bits & 0xffu)};
        }

        static std::array<char, 4> encodeBe32(const std::uint32_t value)
        {
            return {static_cast<char>((value >> 24) & 0xffu),
                    static_cast<char>((value >> 16) & 0xffu),
                    static_cast<char>((value >> 8) & 0xffu),
                    static_cast<char>(value & 0xffu)};
        }

        static std::vector<char>
        readOriginalSampleBytes(std::istream &input, const ParsedFile &parsed,
                                const std::int64_t channel,
                                const std::int64_t frame)
        {
            const auto sampleIndex =
                frame * static_cast<std::int64_t>(parsed.channelCount) + channel;
            const auto byteOffset = static_cast<std::streamoff>(
                parsed.soundDataOffset +
                sampleIndex * static_cast<std::int64_t>(
                                  sampleByteWidth(parsed.sampleFormat)));
            std::vector<char> encoded(sampleByteWidth(parsed.sampleFormat));
            input.seekg(byteOffset, std::ios::beg);
            if (!input.read(encoded.data(),
                            static_cast<std::streamsize>(encoded.size())))
            {
                throw std::runtime_error("Failed to read original AIFF sample bytes");
            }
            return encoded;
        }

        static void patchSamplesInPlace(const cupuacu::State *state,
                                        std::fstream &io,
                                        const ParsedFile &parsed)
        {
            cupuacu::file::preservation::patchDirtySamplesInPlace(
                state, io, sampleByteWidth(parsed.sampleFormat),
                static_cast<std::size_t>(parsed.channelCount),
                parsed.soundDataOffset,
                [&](const float sample)
                {
                    if (parsed.sampleFormat == cupuacu::SampleFormat::FLOAT32)
                    {
                        return encodeFloat32Be(sample);
                    }
                    const auto quantized = quantizeIntegerPcmSample(
                        parsed.sampleFormat, sample, false);
                    return encodeSampleBytes(parsed.sampleFormat, quantized);
                });
        }

        static void patchSoundDataInPlace(const cupuacu::State *state,
                                          const std::filesystem::path &inputPath)
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

                    const ParsedFile parsed = AiffParser::parseFile(outputPath);
                    patchSamplesInPlace(state, io, parsed);
                    io.flush();
                });
        }

        static void copyPrefix(std::istream &input, std::ostream &output,
                               const ParsedFile &parsed)
        {
            const auto *ssndChunk = parsed.findChunk("SSND");
            if (ssndChunk == nullptr)
            {
                throw std::runtime_error("SSND chunk not found");
            }

            cupuacu::file::preservation::copyByteRange(
                input, output, 0, ssndChunk->headerOffset,
                "Failed to read bytes before SSND chunk");
        }

        static void writeSoundChunk(const cupuacu::State *state, std::istream &input,
                                    std::ostream &output,
                                    const ParsedFile &parsed)
        {
            output.write("SSND", 4);

            const auto &document = state->getActiveDocumentSession().document;
            const auto encodedSamples =
                cupuacu::file::preservation::buildEncodedSamples(
                    state, input, sampleByteWidth(document.getSampleFormat()),
                    static_cast<std::size_t>(document.getChannelCount()),
                    [&](std::istream &sourceInput, const std::int64_t channel,
                        const std::int64_t frame)
                    {
                        return readOriginalSampleBytes(sourceInput, parsed, channel,
                                                       frame);
                    },
                    [&](const float sample)
                    {
                        if (document.getSampleFormat() ==
                            cupuacu::SampleFormat::FLOAT32)
                        {
                            return encodeFloat32Be(sample);
                        }
                        const auto quantized = quantizeIntegerPcmSample(
                            document.getSampleFormat(), sample, false);
                        return encodeSampleBytes(document.getSampleFormat(),
                                                 quantized);
                    });

            const std::uint32_t soundDataSize = static_cast<std::uint32_t>(
                encodedSamples.size());
            const std::uint32_t ssndPayloadSize =
                static_cast<std::uint32_t>(8 + parsed.ssndOffset + soundDataSize);
            const auto payloadSizeBytes = encodeBe32(ssndPayloadSize);
            output.write(payloadSizeBytes.data(),
                         static_cast<std::streamsize>(payloadSizeBytes.size()));

            const auto offsetBytes = encodeBe32(parsed.ssndOffset);
            const auto blockSizeBytes = encodeBe32(parsed.ssndBlockSize);
            output.write(offsetBytes.data(),
                         static_cast<std::streamsize>(offsetBytes.size()));
            output.write(blockSizeBytes.data(),
                         static_cast<std::streamsize>(blockSizeBytes.size()));

            if (parsed.ssndOffset > 0)
            {
                cupuacu::file::preservation::copyByteRange(
                    input, output,
                    parsed.findChunk("SSND")->payloadOffset + 8, parsed.ssndOffset,
                    "Failed to read AIFF SSND offset padding");
            }

            if (!encodedSamples.empty())
            {
                output.write(encodedSamples.data(),
                             static_cast<std::streamsize>(encodedSamples.size()));
            }

            if ((ssndPayloadSize & 1u) != 0u)
            {
                output.put('\0');
            }
        }

        static void copySuffix(std::istream &input, std::ostream &output,
                               const ParsedFile &parsed)
        {
            const auto *ssndChunk = parsed.findChunk("SSND");
            if (ssndChunk == nullptr)
            {
                throw std::runtime_error("SSND chunk not found");
            }

            cupuacu::file::preservation::copySuffix(
                input, output,
                ssndChunk->payloadOffset + ssndChunk->paddedPayloadSize,
                "Failed to read bytes after SSND chunk");
        }

        static std::size_t updatedCommSampleFrameCountOffset(
            const ParsedFile &parsed, const std::uint32_t newSoundDataSize)
        {
            const auto *commChunk = parsed.findChunk("COMM");
            const auto *ssndChunk = parsed.findChunk("SSND");
            if (commChunk == nullptr || ssndChunk == nullptr)
            {
                throw std::runtime_error("COMM or SSND chunk not found");
            }

            if (commChunk->headerOffset < ssndChunk->headerOffset)
            {
                return parsed.commSampleFrameCountOffset;
            }

            const std::size_t newSsndPayloadSize =
                static_cast<std::size_t>(8 + parsed.ssndOffset + newSoundDataSize);
            const std::size_t newSsndPaddedPayloadSize =
                newSsndPayloadSize + (newSsndPayloadSize & 1u);
            const auto delta = static_cast<std::ptrdiff_t>(
                newSsndPaddedPayloadSize) -
                               static_cast<std::ptrdiff_t>(
                                   ssndChunk->paddedPayloadSize);
            return static_cast<std::size_t>(
                static_cast<std::ptrdiff_t>(parsed.commSampleFrameCountOffset) +
                delta);
        }

        static void updateCommSampleFrameCount(std::ostream &output,
                                               const ParsedFile &parsed,
                                               const std::uint32_t frameCount,
                                               const std::uint32_t soundDataSize)
        {
            const auto currentPosition = output.tellp();
            if (currentPosition < 0)
            {
                throw std::runtime_error(
                    "Failed to determine output AIFF file size");
            }

            const auto fieldOffset =
                updatedCommSampleFrameCountOffset(parsed, soundDataSize);
            const auto encodedFrameCount = encodeBe32(frameCount);
            output.seekp(static_cast<std::streamoff>(fieldOffset), std::ios::beg);
            output.write(encodedFrameCount.data(),
                         static_cast<std::streamsize>(encodedFrameCount.size()));
            output.seekp(currentPosition, std::ios::beg);
        }

        static void updateFormSizeField(std::ostream &output,
                                        const ParsedFile &parsed)
        {
            const auto fileSizePosition = output.tellp();
            if (fileSizePosition < 0)
            {
                throw std::runtime_error(
                    "Failed to determine output AIFF file size");
            }
            if (fileSizePosition < static_cast<std::streamoff>(8))
            {
                throw std::runtime_error("Output AIFF file is too small");
            }

            const auto fileSize = static_cast<std::uint64_t>(fileSizePosition);
            const auto formDataSize = fileSize - 8;
            if (formDataSize > std::numeric_limits<std::uint32_t>::max())
            {
                throw std::runtime_error("Output AIFF file is too large");
            }

            const auto encodedSize =
                encodeBe32(static_cast<std::uint32_t>(formDataSize));
            output.seekp(static_cast<std::streamoff>(parsed.formSizeOffset),
                         std::ios::beg);
            output.write(encodedSize.data(),
                         static_cast<std::streamsize>(encodedSize.size()));
            output.seekp(fileSizePosition, std::ios::beg);
            if (!output)
            {
                throw std::runtime_error("Failed to update FORM size field");
            }
        }

        static bool hasMarkerChunks(const ParsedFile &parsed)
        {
            return std::any_of(parsed.chunks.begin(), parsed.chunks.end(),
                               [](const ChunkInfo &chunk)
                               { return markers::isMarkerChunk(chunk); });
        }

        static void rewriteWholeFileWithMarkers(
            cupuacu::State *state, const std::filesystem::path &referencePath,
            const std::filesystem::path &outputPath, const ParsedFile &parsed)
        {
            cupuacu::file::writeFileAtomically(
                outputPath,
                [&](const std::filesystem::path &temporaryPath)
                {
                    auto input = cupuacu::file::preservation::openInputFileStream(
                        referencePath);
                    auto output = cupuacu::file::preservation::openOutputFileStream(
                        temporaryPath);

                    cupuacu::file::preservation::copyByteRange(
                        input, output, 0, 12, "Failed to copy AIFF header");

                    bool insertedMarkers = false;
                    for (const auto &chunk : parsed.chunks)
                    {
                        if (!insertedMarkers &&
                            (markers::isMarkerChunk(chunk) ||
                             std::memcmp(chunk.id, "SSND", 4) == 0))
                        {
                            markers::writeMarkerChunks(
                                output,
                                state->getActiveDocumentSession().document.getMarkers());
                            insertedMarkers = true;
                        }

                        if (markers::isMarkerChunk(chunk))
                        {
                            continue;
                        }

                        if (std::memcmp(chunk.id, "SSND", 4) == 0)
                        {
                            writeSoundChunk(state, input, output, parsed);
                            continue;
                        }

                        cupuacu::file::preservation::copyByteRange(
                            input, output, chunk.headerOffset,
                            8 + chunk.paddedPayloadSize,
                            "Failed to copy AIFF chunk");
                    }

                    if (!insertedMarkers)
                    {
                        markers::writeMarkerChunks(
                            output,
                            state->getActiveDocumentSession().document.getMarkers());
                    }

                    const auto soundDataSize = expectedSoundDataSizeBytes(state);
                    updateCommSampleFrameCount(
                        output, parsed,
                        static_cast<std::uint32_t>(
                            state->getActiveDocumentSession().document.getFrameCount()),
                        soundDataSize);
                    updateFormSizeField(output, parsed);
                    output.flush();
                });
        }

    public:
        static void writePreservingAiffFile(
            cupuacu::State *state, const std::filesystem::path &referencePath,
            const std::filesystem::path &outputPath,
            const cupuacu::file::AudioExportSettings &settings)
        {
            if (state == nullptr)
            {
                throw std::invalid_argument("State is null");
            }

            const auto support =
                AiffPreservationSupport::assessAgainstReference(state, settings);
            if (!support.supported)
            {
                throw std::runtime_error(support.reason);
            }
            const ParsedFile parsed = AiffParser::parseFile(referencePath);

            const bool markersRequireRewrite =
                !state->getActiveDocumentSession().document.getMarkers().empty();

            if (!markersRequireRewrite)
            {
                if (referencePath == outputPath &&
                    canPatchSoundDataInPlace(state, parsed))
                {
                    patchSoundDataInPlace(state, referencePath);
                    return;
                }

                cupuacu::file::preservation::rewriteChunkPreserving(
                    referencePath, outputPath,
                    [&](std::istream &input, std::ostream &output)
                    {
                        copyPrefix(input, output, parsed);
                    },
                    [&](std::istream &input, std::ostream &output)
                    {
                        writeSoundChunk(state, input, output, parsed);
                    },
                    [&](std::istream &input, std::ostream &output)
                    {
                        copySuffix(input, output, parsed);
                    },
                    [&](std::ostream &output)
                    {
                        const auto soundDataSize =
                            expectedSoundDataSizeBytes(state);
                        updateCommSampleFrameCount(
                            output, parsed,
                            static_cast<std::uint32_t>(
                                state->getActiveDocumentSession().document.getFrameCount()),
                            soundDataSize);
                        updateFormSizeField(output, parsed);
                    });
                return;
            }

            rewriteWholeFileWithMarkers(state, referencePath, outputPath, parsed);
        }

        static void overwritePreservingAiffFile(cupuacu::State *state)
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
            writePreservingAiffFile(state, referenceFile, session.currentFile,
                                    *settings);
        }
    };
} // namespace cupuacu::file::aiff
