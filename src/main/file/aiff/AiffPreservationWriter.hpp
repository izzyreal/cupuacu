#pragma once

#include "../../State.hpp"
#include "../FileIo.hpp"
#include "../SampleQuantization.hpp"
#include "AiffParser.hpp"
#include "AiffPreservationSupport.hpp"

#include <array>
#include <cmath>
#include <cstring>
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

        static std::uint32_t expectedSoundDataSizeBytes(const cupuacu::State *state)
        {
            const auto &document = state->getActiveDocumentSession().document;
            return static_cast<std::uint32_t>(
                document.getFrameCount() * document.getChannelCount() *
                static_cast<int64_t>(sizeof(std::int16_t)));
        }

        static bool canPatchSoundDataInPlace(const cupuacu::State *state,
                                             const ParsedFile &parsed)
        {
            if (state == nullptr || !parsed.isPcm16 || parsed.commChunkCount != 1 ||
                parsed.ssndChunkCount != 1)
            {
                return false;
            }

            const auto &document = state->getActiveDocumentSession().document;
            if (document.getChannelCount() != parsed.channelCount ||
                document.getSampleRate() != parsed.sampleRate)
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

        static std::array<char, 4> encodeBe32(const std::uint32_t value)
        {
            return {static_cast<char>((value >> 24) & 0xffu),
                    static_cast<char>((value >> 16) & 0xffu),
                    static_cast<char>((value >> 8) & 0xffu),
                    static_cast<char>(value & 0xffu)};
        }

        static std::array<char, sizeof(std::int16_t)>
        readOriginalSampleBytes(std::istream &input, const ParsedFile &parsed,
                                const std::int64_t channel,
                                const std::int64_t frame)
        {
            const auto sampleIndex =
                frame * static_cast<std::int64_t>(parsed.channelCount) + channel;
            const auto byteOffset = static_cast<std::streamoff>(
                parsed.soundDataOffset + sampleIndex * sizeof(std::int16_t));
            std::array<char, sizeof(std::int16_t)> encoded{};
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
            auto buffer = state->getActiveDocumentSession().document.getAudioBuffer();

            const std::size_t frames = static_cast<std::size_t>(
                state->getActiveDocumentSession().document.getFrameCount());
            const std::size_t channels = static_cast<std::size_t>(parsed.channelCount);

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
                    const auto encoded = encodeBe16(quantized);
                    const std::size_t sampleIndex = frame * channels + channel;
                    const std::size_t byteOffset =
                        parsed.soundDataOffset + sampleIndex * sizeof(std::int16_t);

                    io.seekp(static_cast<std::streamoff>(byteOffset), std::ios::beg);
                    io.write(encoded.data(),
                             static_cast<std::streamsize>(encoded.size()));
                    if (!io)
                    {
                        throw std::runtime_error(
                            "Failed to patch AIFF sample bytes");
                    }
                }
            }
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

            input.clear();
            input.seekg(0, std::ios::beg);
            std::vector<char> buffer(ssndChunk->headerOffset);
            if (!buffer.empty() &&
                !input.read(buffer.data(),
                            static_cast<std::streamsize>(buffer.size())))
            {
                throw std::runtime_error("Failed to read bytes before SSND chunk");
            }
            if (!buffer.empty())
            {
                output.write(buffer.data(),
                             static_cast<std::streamsize>(buffer.size()));
            }
        }

        static void writeSoundChunk(const cupuacu::State *state, std::istream &input,
                                    std::ostream &output,
                                    const ParsedFile &parsed)
        {
            const auto &document = state->getActiveDocumentSession().document;
            const std::size_t frames =
                static_cast<std::size_t>(document.getFrameCount());
            const std::size_t channels =
                static_cast<std::size_t>(document.getChannelCount());

            output.write("SSND", 4);

            auto buffer = document.getAudioBuffer();

            std::vector<std::int16_t> interleaved(frames * channels);
            for (std::size_t frame = 0; frame < frames; ++frame)
            {
                for (std::size_t channel = 0; channel < channels; ++channel)
                {
                    const auto channelIndex = static_cast<std::int64_t>(channel);
                    const auto frameIndex = static_cast<std::int64_t>(frame);
                    const float sample = buffer->getSample(channelIndex, frameIndex);
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

            const std::uint32_t soundDataSize = static_cast<std::uint32_t>(
                interleaved.size() * sizeof(std::int16_t));
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
                std::vector<char> offsetPadding(parsed.ssndOffset);
                input.clear();
                input.seekg(static_cast<std::streamoff>(
                                parsed.findChunk("SSND")->payloadOffset + 8),
                            std::ios::beg);
                if (!input.read(offsetPadding.data(),
                                static_cast<std::streamsize>(offsetPadding.size())))
                {
                    throw std::runtime_error(
                        "Failed to read AIFF SSND offset padding");
                }
                output.write(offsetPadding.data(),
                             static_cast<std::streamsize>(offsetPadding.size()));
            }

            for (const auto sample : interleaved)
            {
                const auto encoded = encodeBe16(sample);
                output.write(encoded.data(),
                             static_cast<std::streamsize>(encoded.size()));
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

            const std::size_t suffixOffset =
                ssndChunk->payloadOffset + ssndChunk->paddedPayloadSize;

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
                throw std::runtime_error("Failed to read bytes after SSND chunk");
            }
            output.write(buffer.data(), suffixSize);
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

            if (referencePath == outputPath &&
                canPatchSoundDataInPlace(state, parsed))
            {
                patchSoundDataInPlace(state, referencePath);
                return;
            }

            writeFileAtomically(
                outputPath,
                [&](const std::filesystem::path &temporaryPath)
                {
                    auto input = openInputFileStream(referencePath);
                    auto output = openOutputFileStream(temporaryPath);
                    copyPrefix(input, output, parsed);
                    writeSoundChunk(state, input, output, parsed);
                    copySuffix(input, output, parsed);
                    const auto soundDataSize =
                        expectedSoundDataSizeBytes(state);
                    updateCommSampleFrameCount(
                        output, parsed,
                        static_cast<std::uint32_t>(
                            state->getActiveDocumentSession().document.getFrameCount()),
                        soundDataSize);
                    updateFormSizeField(output, parsed);
                    output.flush();
                });
        }
    };
} // namespace cupuacu::file::aiff
