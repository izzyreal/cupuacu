#pragma once

#include "M4aAtoms.hpp"

#include "../../SampleFormat.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace cupuacu::file::m4a
{
    struct M4aAlacPcmData
    {
        std::uint32_t sampleRate = 0;
        std::uint16_t channels = 0;
        std::uint16_t bitDepth = 0;
        std::uint32_t frameCount = 0;
        cupuacu::SampleFormat sampleFormat = cupuacu::SampleFormat::Unknown;
        std::vector<std::uint8_t> interleavedPcmBytes;
        std::vector<cupuacu::DocumentMarker> markers;
    };

    [[nodiscard]] M4aAlacPcmData readAlacM4a(const Bytes &bytes);
    [[nodiscard]] M4aAlacPcmData
    readAlacM4aFile(const std::filesystem::path &path);
} // namespace cupuacu::file::m4a
