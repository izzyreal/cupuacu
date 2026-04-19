#pragma once

#include "../../State.hpp"
#include "../ChunkedPreservationRewrite.hpp"
#include "../Pcm16PreservationIO.hpp"
#include "WavParser.hpp"
#include "WavPreservationSupport.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace cupuacu::file::wav
{
    class WavPreservationWriter
    {
    private:
        static std::uint32_t expectedDataSizeBytes(const cupuacu::State *state)
        {
            return cupuacu::file::preservation::
                expectedInterleavedPcm16DataSizeBytes(state);
        }

        static bool canPatchDataChunkInPlace(const cupuacu::State *state,
                                             const ParsedFile &parsed)
        {
            if (state == nullptr || !parsed.isPcm16 || parsed.fmtChunkCount != 1 ||
                parsed.dataChunkCount != 1)
            {
                return false;
            }

            const auto &document = state->getActiveDocumentSession().document;
            if (document.getChannelCount() != parsed.channelCount ||
                document.getSampleRate() != parsed.sampleRate)
            {
                return false;
            }

            const auto *dataChunk = parsed.findChunk("data");
            if (dataChunk == nullptr)
            {
                return false;
            }

            return dataChunk->payloadSize == expectedDataSizeBytes(state);
        }

        static std::array<char, sizeof(std::int16_t)>
        encodeLe16(const std::int16_t value)
        {
            const auto encoded = static_cast<std::uint16_t>(value);
            return {static_cast<char>(encoded & 0xffu),
                    static_cast<char>((encoded >> 8) & 0xffu)};
        }

        static std::array<char, sizeof(std::int16_t)>
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
                dataChunk->payloadOffset + sampleIndex * sizeof(std::int16_t));
            std::array<char, sizeof(std::int16_t)> encoded{};
            input.seekg(byteOffset, std::ios::beg);
            if (!input.read(encoded.data(),
                            static_cast<std::streamsize>(encoded.size())))
            {
                throw std::runtime_error("Failed to read original WAV sample bytes");
            }
            return encoded;
        }

        static void patchSamplesInPlace(const cupuacu::State *state,
                                        std::fstream &io,
                                        const ParsedFile &parsed)
        {
            const auto *dataChunk = parsed.findChunk("data");
            if (dataChunk == nullptr)
            {
                throw std::runtime_error("data chunk not found");
            }

            cupuacu::file::preservation::patchDirtyPcm16SamplesInPlace(
                state, io, static_cast<std::size_t>(parsed.channelCount),
                dataChunk->payloadOffset,
                [](const std::int16_t sample) { return encodeLe16(sample); });
        }

        static void patchDataChunkInPlace(const cupuacu::State *state,
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

                    const ParsedFile parsed = WavParser::parseFile(outputPath);
                    patchSamplesInPlace(state, io, parsed);
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

        static void writeDataChunk(const cupuacu::State *state, std::istream &input,
                                   std::ostream &output,
                                   const ParsedFile &parsed)
        {
            output.write("data", 4);

            const auto &document = state->getActiveDocumentSession().document;
            const auto encodedSamples =
                cupuacu::file::preservation::buildEncodedPcm16Samples(
                    state, input,
                    static_cast<std::size_t>(document.getChannelCount()),
                    [&](std::istream &sourceInput, const std::int64_t channel,
                        const std::int64_t frame)
                    {
                        return readOriginalSampleBytes(sourceInput, parsed, channel,
                                                       frame);
                    },
                    [](const std::int16_t sample) { return encodeLe16(sample); });

            const std::uint32_t dataSize = static_cast<std::uint32_t>(
                encodedSamples.size());
            output.write(reinterpret_cast<const char *>(&dataSize), 4);
            if (dataSize > 0)
            {
                output.write(encodedSamples.data(),
                             static_cast<std::streamsize>(dataSize));
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
            const ParsedFile parsed = WavParser::parseFile(referencePath);

            if (referencePath == outputPath && canPatchDataChunkInPlace(state, parsed))
            {
                patchDataChunkInPlace(state, referencePath);
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
                    writeDataChunk(state, input, output, parsed);
                },
                [&](std::istream &input, std::ostream &output)
                {
                    copySuffix(input, output, parsed);
                },
                [&](std::ostream &output) { updateRiffSizeField(output, parsed); });
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
