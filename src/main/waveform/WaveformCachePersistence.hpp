#pragma once

#include "../Document.hpp"
#include "../Paths.hpp"
#include "../gui/WaveformCache.hpp"
#include "SourcePeaks.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <memory>
#include <functional>
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

    [[nodiscard]] bool savePersistentWaveformCache(
        const cupuacu::DocumentSession &session,
        const std::filesystem::path &cacheRoot);

    enum class CacheSaveScheduleResult
    {
        Scheduled,
        Unavailable,
        Busy,
    };

    struct PersistentCacheSnapshot
    {
        std::filesystem::path root;
        PersistentCacheKey key;
        std::vector<gui::WaveformCache::BuildState> channels;
        // Optional worker-side preparation, also used for deterministic I/O
        // delay/failure tests. Never called by capture or admission.
        std::function<void()> beforeWrite;
        std::shared_ptr<const SourcePeaks> sourcePeaks;
    };
    std::shared_ptr<const PersistentCacheSnapshot>
    capturePersistentWaveformCache(const cupuacu::DocumentSession &,
                                   const std::filesystem::path &root);
    std::shared_ptr<const PersistentCacheSnapshot>
    capturePersistentSourcePeaks(const std::string &source, const Document &,
                                 const std::filesystem::path &root,
                                 std::shared_ptr<const SourcePeaks>);
    std::shared_ptr<const SourcePeaks>
    loadPersistentSourcePeaks(const std::string &source, const Document &,
                              const std::filesystem::path &root,
                              std::shared_ptr<storage::DecodedBlockCache>,
                              const std::function<bool()> &cancel = {});
    CacheSaveScheduleResult schedulePersistentWaveformCache(
        const std::shared_ptr<const PersistentCacheSnapshot> &);

    // Retains shared peak pages only, never the document's audio. A full queue
    // returns Busy so the caller can retry without blocking the event loop.
    [[nodiscard]] CacheSaveScheduleResult
    schedulePersistentWaveformCache(const cupuacu::DocumentSession &session,
                                    const Paths &paths);
    [[nodiscard]] bool hasScheduledPersistentWaveformCacheWork();
    void flushScheduledPersistentWaveformCaches();

    [[nodiscard]] bool loadPersistentWaveformCache(
        cupuacu::DocumentSession &session, const Paths &paths);

    [[nodiscard]] bool loadPersistentWaveformCache(
        cupuacu::DocumentSession &session,
        const std::filesystem::path &cacheRoot);
} // namespace cupuacu::waveform
