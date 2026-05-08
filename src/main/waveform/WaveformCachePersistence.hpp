#pragma once

#include "../Document.hpp"
#include "../Paths.hpp"
#include "../gui/WaveformCache.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace cupuacu
{
    struct DocumentSession;
}

namespace cupuacu::waveform
{
    struct PersistentCacheKey
    {
        static constexpr uint32_t FORMAT_VERSION = 1;

        std::string sourcePath;
        uint64_t sourceFileSize = 0;
        int64_t sourceLastWriteTimeNs = 0;
        SampleFormat sampleFormat = SampleFormat::Unknown;
        int sampleRate = 0;
        int64_t channelCount = 0;
        int64_t frameCount = 0;

        bool operator==(const PersistentCacheKey &) const = default;

        [[nodiscard]] std::string cacheBasename() const;
        [[nodiscard]] std::filesystem::path cachePath(const Paths &paths) const;
    };

    [[nodiscard]] std::optional<PersistentCacheKey>
    makePersistentCacheKey(const std::string &sourceFilePath,
                           const Document &document);

    [[nodiscard]] bool savePersistentWaveformCache(
        const cupuacu::DocumentSession &session, const Paths &paths);

    [[nodiscard]] bool loadPersistentWaveformCache(
        cupuacu::DocumentSession &session, const Paths &paths);
} // namespace cupuacu::waveform
