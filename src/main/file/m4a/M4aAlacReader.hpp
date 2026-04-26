#pragma once

#include "M4aAtoms.hpp"

#include "../../SampleFormat.hpp"
#include "../alac/AlacCodec.hpp"

#include <cstdint>
#include <functional>
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

    using M4aReadProgressCallback =
        std::function<void(std::uint64_t bytesRead, std::uint64_t totalBytes)>;
    using M4aDecodedPcmBlockCallback =
        std::function<void(const std::uint8_t *interleavedPcmBytes,
                           std::uint32_t pcmByteCount,
                           std::uint32_t frameCount,
                           std::uint16_t channels,
                           std::uint16_t bitDepth)>;

    struct M4aAlacFileInfo
    {
        std::uint32_t sampleRate = 0;
        std::uint16_t channels = 0;
        std::uint16_t bitDepth = 0;
        std::uint32_t frameCount = 0;
        cupuacu::SampleFormat sampleFormat = cupuacu::SampleFormat::Unknown;
        std::vector<cupuacu::DocumentMarker> markers;
    };
    using M4aAlacFileInfoCallback =
        std::function<void(const M4aAlacFileInfo &)>;

    [[nodiscard]] M4aAlacPcmData
    readAlacM4a(const Bytes &bytes,
                cupuacu::file::alac::DecodeProgressCallback progressCallback =
                    {});
    [[nodiscard]] M4aAlacFileInfo
    streamAlacM4aFile(const std::filesystem::path &path,
                      M4aDecodedPcmBlockCallback decodedPcmBlockCallback,
                      M4aAlacFileInfoCallback fileInfoCallback = {},
                      cupuacu::file::alac::DecodeProgressCallback
                          progressCallback = {},
                      M4aReadProgressCallback readProgressCallback = {});
    [[nodiscard]] M4aAlacPcmData
    readAlacM4aFile(const std::filesystem::path &path,
                    cupuacu::file::alac::DecodeProgressCallback
                        progressCallback = {},
                    M4aReadProgressCallback readProgressCallback = {});
} // namespace cupuacu::file::m4a
