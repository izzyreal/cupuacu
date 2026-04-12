#pragma once

#include "../../State.hpp"
#include "../FileIo.hpp"
#include "../SampleQuantization.hpp"
#include "WavParser.hpp"

#include <array>
#include <cstring>
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
        static std::ifstream openInputFileStream(const std::filesystem::path &path)
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

        static std::ofstream openOutputFileStream(const std::filesystem::path &path)
        {
            std::ofstream result(path, std::ios::binary | std::ios::trunc);
            if (!result.is_open())
            {
                throw cupuacu::file::detail::makeIoFailure(
                    "Failed to open output file",
                    cupuacu::file::detail::describeErrno(errno));
            }
            return result;
        }

        static std::uint32_t expectedDataSizeBytes(const cupuacu::State *state)
        {
            const auto &document = state->getActiveDocumentSession().document;
            return static_cast<std::uint32_t>(
                document.getFrameCount() * document.getChannelCount() *
                static_cast<int64_t>(sizeof(std::int16_t)));
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

            auto buffer = state->getActiveDocumentSession().document.getAudioBuffer();

            const std::size_t frames = static_cast<std::size_t>(
                state->getActiveDocumentSession().document.getFrameCount());
            const std::size_t channels = static_cast<std::size_t>(parsed.channelCount);
            const std::size_t sampleDataOffset = dataChunk->payloadOffset;

            for (std::size_t frame = 0; frame < frames; ++frame)
            {
                for (std::size_t channel = 0; channel < channels; ++channel)
                {
                    const auto channelIndex = static_cast<std::int64_t>(channel);
                    const auto frameIndex = static_cast<std::int64_t>(frame);
                    if (!buffer->isDirty(channelIndex, frameIndex))
                    {
                        continue;
                    }

                    const float sample = buffer->getSample(channelIndex, frameIndex);
                    const auto quantized = static_cast<std::int16_t>(
                        quantizeIntegerPcmSample(cupuacu::SampleFormat::PCM_S16,
                                                 sample, false));
                    const auto encoded = encodeLe16(quantized);
                    const std::size_t sampleIndex = frame * channels + channel;
                    const std::size_t byteOffset =
                        sampleDataOffset + sampleIndex * sizeof(std::int16_t);

                    io.seekp(static_cast<std::streamoff>(byteOffset), std::ios::beg);
                    io.write(encoded.data(),
                             static_cast<std::streamsize>(encoded.size()));
                    if (!io)
                    {
                        throw std::runtime_error(
                            "Failed to patch WAV sample bytes");
                    }
                }
            }
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

            input.clear();
            input.seekg(0, std::ios::beg);
            std::vector<char> buffer(dataChunk->headerOffset);
            if (!buffer.empty() &&
                !input.read(buffer.data(), static_cast<std::streamsize>(buffer.size())))
            {
                throw std::runtime_error("Failed to read bytes before data chunk");
            }
            if (!buffer.empty())
            {
                output.write(buffer.data(),
                             static_cast<std::streamsize>(buffer.size()));
            }
        }

        static void writeDataChunk(const cupuacu::State *state, std::istream &input,
                                   std::ostream &output,
                                   const ParsedFile &parsed)
        {
            const auto &document = state->getActiveDocumentSession().document;
            const std::size_t frames =
                static_cast<std::size_t>(document.getFrameCount());
            const std::size_t channels =
                static_cast<std::size_t>(document.getChannelCount());

            output.write("data", 4);

            auto buffer = document.getAudioBuffer();

            std::vector<std::int16_t> interleaved(frames * channels);
            for (std::size_t frame = 0; frame < frames; ++frame)
            {
                for (std::size_t channel = 0; channel < channels; ++channel)
                {
                    const auto channelIndex = static_cast<std::int64_t>(channel);
                    const auto frameIndex = static_cast<std::int64_t>(frame);
                    const float sample = buffer->getSample(
                        static_cast<std::int64_t>(channel),
                        static_cast<std::int64_t>(frame));
                    const auto provenance =
                        document.getSampleProvenance(channelIndex, frameIndex);
                    if (!buffer->isDirty(channelIndex, frameIndex) &&
                        provenance.sourceId == document.getPreservationSourceId() &&
                        provenance.frameIndex >= 0)
                    {
                        const auto encoded = readOriginalSampleBytes(
                            input, parsed, channelIndex, provenance.frameIndex);
                        const auto targetIndex = frame * channels + channel;
                        std::memcpy(&interleaved[targetIndex], encoded.data(),
                                    sizeof(std::int16_t));
                        continue;
                    }

                    interleaved[frame * channels + channel] = static_cast<std::int16_t>(
                        quantizeIntegerPcmSample(cupuacu::SampleFormat::PCM_S16,
                                                 sample, false));
                }
            }

            const std::uint32_t dataSize = static_cast<std::uint32_t>(
                interleaved.size() * sizeof(std::int16_t));
            output.write(reinterpret_cast<const char *>(&dataSize), 4);
            if (dataSize > 0)
            {
                output.write(reinterpret_cast<const char *>(interleaved.data()),
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

            const std::size_t suffixOffset =
                dataChunk->payloadOffset + dataChunk->paddedPayloadSize;

            input.clear();
            input.seekg(0, std::ios::end);
            const std::streamoff fileEnd = input.tellg();
            if (fileEnd <= static_cast<std::streamoff>(suffixOffset))
            {
                return;
            }

            const std::streamsize suffixSize =
                fileEnd - static_cast<std::streamoff>(suffixOffset);
            std::vector<char> buffer(static_cast<std::size_t>(suffixSize));
            input.seekg(static_cast<std::streamoff>(suffixOffset), std::ios::beg);
            if (!input.read(buffer.data(), suffixSize))
            {
                throw std::runtime_error("Failed to read bytes after data chunk");
            }
            output.write(buffer.data(), suffixSize);
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
        static void overwritePreservingWavFile(cupuacu::State *state)
        {
            if (state == nullptr)
            {
                throw std::invalid_argument("State is null");
            }

            const std::filesystem::path inputPath(
                state->getActiveDocumentSession().currentFile);
            const ParsedFile parsed = WavParser::parseFile(inputPath);
            if (!parsed.isPcm16)
            {
                throw std::invalid_argument("Not a 16-bit PCM WAV file");
            }
            if (parsed.fmtChunkCount != 1)
            {
                throw std::runtime_error(
                    "Unsupported WAV structure: expected exactly one fmt chunk");
            }
            if (parsed.dataChunkCount != 1)
            {
                throw std::runtime_error(
                    "Unsupported WAV structure: expected exactly one data chunk");
            }
            if (parsed.findChunk("data") == nullptr)
            {
                throw std::runtime_error("data chunk not found");
            }

            if (canPatchDataChunkInPlace(state, parsed))
            {
                patchDataChunkInPlace(state, inputPath);
                return;
            }

            writeFileAtomically(
                inputPath,
                [&](const std::filesystem::path &outputPath)
                {
                    auto input = openInputFileStream(inputPath);
                    auto output = openOutputFileStream(outputPath);
                    copyPrefix(input, output, parsed);
                    writeDataChunk(state, input, output, parsed);
                    copySuffix(input, output, parsed);
                    updateRiffSizeField(output, parsed);
                    output.flush();
                });
        }
    };
} // namespace cupuacu::file::wav
