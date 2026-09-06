#pragma once
#include "AudioEditRevision.hpp"
#include <nlohmann/json.hpp>
#include <functional>
#include <set>

namespace cupuacu::storage
{
    // Worker-owned append-only records. A small atomic manifest selects a
    // committed prefix; unfinished records never replace the previous root.
    class RevisionArchive : public std::enable_shared_from_this<RevisionArchive>
    {
    public:
        using Json = nlohmann::json;
        struct Stats
        {
            uint64_t sampleBytes = 0, sourceBytes = 0, metadataBytes = 0,
                     nodes = 0;
        };
        static std::shared_ptr<RevisionArchive>
        open(const std::filesystem::path &manifest);
        static void remove(const std::filesystem::path &manifest);
        static bool recognizes(const std::filesystem::path &manifest);
        ~RevisionArchive();
        std::mutex operationMutex;
        Stats stats;
        uint64_t save(const std::shared_ptr<const AudioEditRevision> &);
        uint64_t saveSource(const std::shared_ptr<const AudioRevision> &);
        std::shared_ptr<const AudioEditRevision> load(uint64_t);
        std::shared_ptr<const AudioRevision> loadSource(uint64_t);
        uint64_t additionalHistoryBytes(
            const std::vector<std::shared_ptr<const AudioEditRevision>> &base,
            const std::shared_ptr<const AudioRevision> &preservation,
            const std::vector<std::shared_ptr<const AudioEditRevision>>
                &history);
        void commit(Json manifest,
                    const std::function<void()> &beforeReplace = {});
        Json readManifest();
        void
        retainClipboardStores(const std::shared_ptr<const AudioEditRevision> &);
        void setCancelCheck(std::function<bool()> check)
        {
            canceled = std::move(check);
        }

    private:
        using Tree = AudioEditRevision::Tree;
        struct StoreCopy
        {
            std::string name;
            std::vector<uint64_t> lengths;
            std::string source;
        };
        std::filesystem::path manifestPath, directory;
        std::atomic_bool removed{false};
        std::mutex publicationMutex;
        uint64_t readLimit = 0;
        std::function<bool()> canceled;
        bool pruneClipboardStores = false;
        std::set<std::string> neededStores;
        void collectUnusedStores();
        void collectUnusedStoresLocked();
        // Stable object identities survive the deferred-release alias wrappers;
        // weak control-block identity does not, and would split restored roots.
        std::map<uint64_t, uint64_t> roots, nodes, sources;
        std::map<uint64_t, StoreCopy> stores;
        std::map<uint64_t, std::weak_ptr<const AudioEditRevision>> loadedRoots;
        std::map<uint64_t, std::weak_ptr<const AudioEditRevision::Node>>
            loadedNodes;
        std::map<uint64_t, std::weak_ptr<const AudioRevision>> loadedSources;
        std::map<std::string, std::weak_ptr<AudioBlockStore>> loadedStores;
        std::shared_ptr<DecodedBlockCache> cache =
            std::make_shared<DecodedBlockCache>(1024 * 1024);
        explicit RevisionArchive(std::filesystem::path);
        void check() const;
        uint64_t append(const Json &);
        Json record(uint64_t);
        uint64_t saveNode(const Tree &);
        Tree loadNode(uint64_t, int depth = 0);
        StoreCopy &copyStore(const AudioRevision &);
    };
} // namespace cupuacu::storage
