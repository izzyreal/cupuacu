#pragma once
#include "../storage/AudioBlockStore.hpp"
#include "SessionStatePersistence.hpp"
#include <functional>
namespace cupuacu
{
    struct DocumentSession;
}
namespace cupuacu::persistence
{
    std::shared_ptr<storage::AudioBlockStore>
    legacyRecoveryStore(const std::filesystem::path &parent);
    // Worker-only. Builds a complete candidate history before publication.
    // Input snapshots and undo payloads are never modified by migration.
    bool migrateLegacyHistory(DocumentSession &,
                              const PersistedOpenDocumentState *,
                              const std::filesystem::path &workingRoot,
                              const std::function<bool()> &cancel);
} // namespace cupuacu::persistence
