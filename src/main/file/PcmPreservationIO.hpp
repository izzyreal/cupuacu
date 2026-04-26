#pragma once

#include "../State.hpp"
#include "FileIo.hpp"
#include "PreservationWriteInput.hpp"
#include "SampleQuantization.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cupuacu::file::preservation
{
    inline void publishPreservationProgress(
        const cupuacu::file::PreservationProgressCallback &progress,
        const std::string &detail, const std::size_t completed,
        const std::size_t total, const double start, const double end)
    {
        if (!progress)
        {
            return;
        }

        const double normalized =
            total > 0
                ? static_cast<double>(completed) / static_cast<double>(total)
                : 1.0;
        progress(detail, std::clamp(start + (end - start) * normalized,
                                    0.0, 1.0));
    }

    inline std::ifstream openInputFileStream(const std::filesystem::path &path)
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

    inline std::ofstream
    openOutputFileStream(const std::filesystem::path &path)
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

    inline std::uint32_t
    expectedInterleavedDataSizeBytes(const cupuacu::State *state,
                                     const std::size_t bytesPerSample)
    {
        const auto &document = state->getActiveDocumentSession().document;
        return static_cast<std::uint32_t>(
            document.getFrameCount() * document.getChannelCount() *
            static_cast<int64_t>(bytesPerSample));
    }

    inline std::uint32_t
    expectedInterleavedDataSizeBytes(
        const cupuacu::Document::ReadLease &document,
        const std::size_t bytesPerSample)
    {
        return static_cast<std::uint32_t>(
            document.getFrameCount() * document.getChannelCount() *
            static_cast<int64_t>(bytesPerSample));
    }

    inline void copyByteRange(std::istream &input, std::ostream &output,
                              const std::size_t startOffset,
                              const std::size_t size,
                              const char *failureMessage)
    {
        input.clear();
        input.seekg(static_cast<std::streamoff>(startOffset), std::ios::beg);

        std::vector<char> buffer(size);
        if (!buffer.empty() &&
            !input.read(buffer.data(), static_cast<std::streamsize>(buffer.size())))
        {
            throw std::runtime_error(failureMessage);
        }

        if (!buffer.empty())
        {
            output.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        }
    }

    inline void copySuffix(std::istream &input, std::ostream &output,
                           const std::size_t suffixOffset,
                           const char *failureMessage)
    {
        input.clear();
        input.seekg(0, std::ios::end);
        const std::streamoff fileEnd = input.tellg();
        if (fileEnd <= static_cast<std::streamoff>(suffixOffset))
        {
            return;
        }

        const std::streamsize suffixSize =
            fileEnd - static_cast<std::streamoff>(suffixOffset);
        copyByteRange(input, output, suffixOffset,
                      static_cast<std::size_t>(suffixSize), failureMessage);
    }

    template <typename EncodeDirtySampleFn>
    void patchDirtySamplesInPlace(const cupuacu::State *state, std::fstream &io,
                                  const std::size_t sampleByteWidth,
                                  const std::size_t channelCount,
                                  const std::size_t sampleDataOffset,
                                  EncodeDirtySampleFn encodeDirtySample)
    {
        auto buffer = state->getActiveDocumentSession().document.getAudioBuffer();
        const std::size_t frames = static_cast<std::size_t>(
            state->getActiveDocumentSession().document.getFrameCount());

        for (std::size_t frame = 0; frame < frames; ++frame)
        {
            for (std::size_t channel = 0; channel < channelCount; ++channel)
            {
                const auto channelIndex = static_cast<std::int64_t>(channel);
                const auto frameIndex = static_cast<std::int64_t>(frame);
                if (!buffer->isDirty(channelIndex, frameIndex))
                {
                    continue;
                }

                const float sample = buffer->getSample(channelIndex, frameIndex);
                const auto encoded = encodeDirtySample(sample);
                const std::size_t sampleIndex = frame * channelCount + channel;
                const std::size_t byteOffset =
                    sampleDataOffset + sampleIndex * sampleByteWidth;

                io.seekp(static_cast<std::streamoff>(byteOffset), std::ios::beg);
                io.write(encoded.data(),
                         static_cast<std::streamsize>(encoded.size()));
                if (!io)
                {
                    throw std::runtime_error(
                        "Failed to patch sample bytes");
                }
            }
        }
    }

    template <typename EncodeDirtySampleFn>
    void patchDirtySamplesInPlace(
        const cupuacu::Document::ReadLease &document, std::fstream &io,
        const std::size_t sampleByteWidth, const std::size_t channelCount,
        const std::size_t sampleDataOffset,
        EncodeDirtySampleFn encodeDirtySample,
        const cupuacu::file::PreservationProgressCallback &progress = {},
        const std::string &progressDetail = {},
        const double progressStart = 0.0,
        const double progressEnd = 1.0)
    {
        const std::size_t frames =
            static_cast<std::size_t>(document.getFrameCount());
        constexpr std::size_t progressStrideFrames = 16384;
        publishPreservationProgress(progress, progressDetail, 0, frames,
                                    progressStart, progressEnd);

        for (std::size_t frame = 0; frame < frames; ++frame)
        {
            for (std::size_t channel = 0; channel < channelCount; ++channel)
            {
                const auto channelIndex = static_cast<std::int64_t>(channel);
                const auto frameIndex = static_cast<std::int64_t>(frame);
                if (!document.isDirty(channelIndex, frameIndex))
                {
                    continue;
                }

                const float sample = document.getSample(channelIndex, frameIndex);
                const auto encoded = encodeDirtySample(sample);
                const std::size_t sampleIndex = frame * channelCount + channel;
                const std::size_t byteOffset =
                    sampleDataOffset + sampleIndex * sampleByteWidth;

                io.seekp(static_cast<std::streamoff>(byteOffset), std::ios::beg);
                io.write(encoded.data(),
                         static_cast<std::streamsize>(encoded.size()));
                if (!io)
                {
                    throw std::runtime_error("Failed to patch sample bytes");
                }
            }

            if ((frame + 1) % progressStrideFrames == 0 || frame + 1 == frames)
            {
                publishPreservationProgress(progress, progressDetail, frame + 1,
                                            frames, progressStart, progressEnd);
            }
        }
    }

    template <typename ReadOriginalBytesFn, typename EncodeDirtySampleFn>
    std::vector<char> buildEncodedSamples(const cupuacu::State *state,
                                          std::istream &input,
                                          const std::size_t sampleByteWidth,
                                          const std::size_t channelCount,
                                          ReadOriginalBytesFn readOriginalBytes,
                                          EncodeDirtySampleFn encodeDirtySample)
    {
        const auto &document = state->getActiveDocumentSession().document;
        const std::size_t frames =
            static_cast<std::size_t>(document.getFrameCount());
        auto buffer = document.getAudioBuffer();

        std::vector<char> encodedSamples;
        encodedSamples.reserve(frames * channelCount * sampleByteWidth);

        for (std::size_t frame = 0; frame < frames; ++frame)
        {
            for (std::size_t channel = 0; channel < channelCount; ++channel)
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
                    const auto encoded =
                        readOriginalBytes(input, channelIndex, provenance.frameIndex);
                    encodedSamples.insert(encodedSamples.end(), encoded.begin(),
                                          encoded.end());
                    continue;
                }

                const auto encoded = encodeDirtySample(sample);
                encodedSamples.insert(encodedSamples.end(), encoded.begin(),
                                      encoded.end());
            }
        }

        return encodedSamples;
    }

    template <typename ReadOriginalBytesFn, typename EncodeDirtySampleFn>
    std::vector<char> buildEncodedSamples(
        const cupuacu::Document::ReadLease &document, std::istream &input,
        const std::size_t sampleByteWidth, const std::size_t channelCount,
        ReadOriginalBytesFn readOriginalBytes,
        EncodeDirtySampleFn encodeDirtySample,
        const cupuacu::file::PreservationProgressCallback &progress = {},
        const std::string &progressDetail = {},
        const double progressStart = 0.0,
        const double progressEnd = 1.0)
    {
        const std::size_t frames =
            static_cast<std::size_t>(document.getFrameCount());
        constexpr std::size_t progressStrideFrames = 16384;

        std::vector<char> encodedSamples;
        encodedSamples.reserve(frames * channelCount * sampleByteWidth);
        publishPreservationProgress(progress, progressDetail, 0, frames,
                                    progressStart, progressEnd);

        for (std::size_t frame = 0; frame < frames; ++frame)
        {
            for (std::size_t channel = 0; channel < channelCount; ++channel)
            {
                const auto channelIndex = static_cast<std::int64_t>(channel);
                const auto frameIndex = static_cast<std::int64_t>(frame);
                const float sample = document.getSample(channelIndex, frameIndex);
                const auto provenance =
                    document.getSampleProvenance(channelIndex, frameIndex);
                if (!document.isDirty(channelIndex, frameIndex) &&
                    provenance.sourceId == document.getPreservationSourceId() &&
                    provenance.frameIndex >= 0)
                {
                    const auto encoded =
                        readOriginalBytes(input, channelIndex, provenance.frameIndex);
                    encodedSamples.insert(encodedSamples.end(), encoded.begin(),
                                          encoded.end());
                    continue;
                }

                const auto encoded = encodeDirtySample(sample);
                encodedSamples.insert(encodedSamples.end(), encoded.begin(),
                                      encoded.end());
            }

            if ((frame + 1) % progressStrideFrames == 0 || frame + 1 == frames)
            {
                publishPreservationProgress(progress, progressDetail, frame + 1,
                                            frames, progressStart, progressEnd);
            }
        }

        return encodedSamples;
    }
} // namespace cupuacu::file::preservation
