#pragma once

#if defined(__APPLE__)

#include "M4aAlacReader.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>

namespace cupuacu::file::m4a
{
    using M4aDecodedFloatBlockCallback =
        std::function<void(const float *interleavedSamples,
                           std::uint32_t frameCount,
                           std::uint16_t channels)>;
    using M4aFloatProgressCallback =
        std::function<void(std::uint32_t framesRead,
                           std::uint32_t totalFrames)>;

    [[nodiscard]] M4aAlacFileInfo
    streamAlacM4aFileWithAudioToolbox(
        const std::filesystem::path &path,
        M4aDecodedFloatBlockCallback decodedFloatBlockCallback,
        M4aAlacFileInfoCallback fileInfoCallback = {},
        M4aFloatProgressCallback progressCallback = {});
} // namespace cupuacu::file::m4a

#endif
