#pragma once
#include "../actions/audio/RevisionEdit.hpp"
#include "../storage/RevisionArchive.hpp"

namespace cupuacu::persistence
{
    struct RevisionCheckpoint
    {
        struct History
        {
            actions::audio::RevisionEditState before, after;
            std::shared_ptr<const storage::AudioEditRevision> copied;
            nlohmann::json details;
        };
        actions::audio::RevisionEditState current, saved;
        std::shared_ptr<const storage::AudioRevision> preservation;
        nlohmann::json metadata;
        std::vector<History> undo, redo;
        std::string historyWarning;
    };
    class RevisionPersistence
    {
    public:
        static std::shared_ptr<RevisionCheckpoint>
        capture(const DocumentSession &, const DocumentTab * = nullptr);
        static void save(const std::filesystem::path &,
                         const RevisionCheckpoint &,
                         const std::function<void()> &beforeReplace = {},
                         uint64_t maxHistoryBytes = UINT64_MAX,
                         const std::function<bool()> &cancel = {});
        static void load(const std::filesystem::path &, DocumentSession &,
                         const std::function<bool()> &cancel = {});
        static bool installHistory(State *, int tabIndex);
    };
} // namespace cupuacu::persistence
