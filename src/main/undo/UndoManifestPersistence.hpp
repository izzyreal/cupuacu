#pragma once

#include "../DocumentTab.hpp"
#include "../State.hpp"
#include "../persistence/SessionStatePersistence.hpp"
#include "UndoStore.hpp"

#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace cupuacu::undo
{
    std::shared_ptr<actions::Undoable> restoreUndoEntry(State *, int,
                                                        const nlohmann::json &);
    [[nodiscard]] std::uint64_t maxRestartUndoStoreBytes();

    [[nodiscard]] bool shouldPersistUndoStoreForRestart(
        const UndoStore::Stats &stats,
        std::uint64_t maxBytes = maxRestartUndoStoreBytes());

    [[nodiscard]] std::string
    describeUndoStoreStats(const UndoStore::Stats &stats);

    [[nodiscard]] std::filesystem::path
    manifestPathForStore(const std::filesystem::path &undoStorePath);

    [[nodiscard]] bool saveUndoManifest(const std::filesystem::path &manifestPath,
                                        const cupuacu::DocumentTab &tab);

    [[nodiscard]] bool restoreUndoManifest(cupuacu::State *state, int tabIndex,
                                           const std::filesystem::path &undoStorePath);

    void pruneUndoStores(const std::filesystem::path &undoRoot,
                         const cupuacu::persistence::PersistedSessionState &state);
} // namespace cupuacu::undo
