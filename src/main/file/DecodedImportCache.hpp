#pragma once
#include "file_loading.hpp"
#include "../concurrency/TaskScheduler.hpp"
#include <map>
#include <mutex>

namespace cupuacu::file
{
    // Disposable, process-leased disk cache. All methods run on bulk workers.
    // Weak live entries also allow immediate reuse while persistence is
    // pending.
    class DecodedImportCache
        : public std::enable_shared_from_this<DecodedImportCache>
    {
    public:
        static constexpr uint64_t defaultByteBudget = 8ull * 1024 * 1024 * 1024;
        explicit DecodedImportCache(std::filesystem::path root,
                                    uint64_t byteBudget = defaultByteBudget);
        static std::string sourceIdentity(const std::filesystem::path &);
        std::unique_ptr<LoadedAudioFile>
        load(const std::string &identity,
             const std::shared_ptr<storage::DecodedBlockCache> &samples,
             const LoadCancelCheck &cancel = {});
        void retain(const std::string &identity, const LoadedAudioFile &,
                    const std::shared_ptr<concurrency::TaskScheduler> &);
        static bool hasPendingWork();
        uint64_t hitCount() const
        {
            return hits.load();
        }
        uint64_t diskHitCount() const
        {
            return diskHits.load();
        }
        // Admission/pruning hooks are useful with small deterministic budgets.
        uint64_t diskBytes() const;
        void prune();

    private:
        struct Entry
        {
            std::weak_ptr<const storage::AudioRevision> audio;
            Document metadata;
            std::optional<AudioExportSettings> settings;
            bool requiresSaveAs = false;
        };
        std::filesystem::path root;
        uint64_t budget;
        std::atomic<uint64_t> hits{0}, diskHits{0};
        std::shared_ptr<const void> lease;
        std::atomic_bool initialized{false}, filling{false};
        void ensureLease();
        std::mutex liveMutex, maintenanceMutex;
        std::map<std::string, Entry> live;
        std::filesystem::path entryPath(const std::string &) const;
        bool makeRoom(uint64_t bytes);
        bool removeEntry(const std::filesystem::path &);
    };
} // namespace cupuacu::file
