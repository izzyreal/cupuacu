#pragma once

#include "M4aAlacReader.hpp"

#include "../../SampleFormat.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

namespace cupuacu::file::m4a
{
    struct M4aAacFileInfo
    {
        std::uint32_t sampleRate = 0;
        std::uint16_t channels = 0;
        std::uint32_t frameCount = 0;
        cupuacu::SampleFormat sampleFormat = cupuacu::SampleFormat::FLOAT32;
        std::vector<cupuacu::DocumentMarker> markers;
    };

    using M4aDecodedFloatBlockCallback =
        std::function<void(const float *interleavedSamples,
                           std::uint32_t frameCount, std::uint16_t channels)>;
    using M4aAacFileInfoCallback = std::function<void(const M4aAacFileInfo &)>;
    using M4aAacDecodeProgressCallback = std::function<void(
        std::uint32_t decodedFrames, std::uint32_t totalFrames)>;

    [[nodiscard]] M4aAacFileInfo
    streamAacM4aFile(const std::filesystem::path &path,
                     M4aDecodedFloatBlockCallback decodedBlockCallback,
                     M4aAacFileInfoCallback fileInfoCallback = {},
                     M4aAacDecodeProgressCallback progressCallback = {},
                     M4aReadProgressCallback readProgressCallback = {});
} // namespace cupuacu::file::m4a
