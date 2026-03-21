#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace cupuacu::persistence
{
    struct PersistedSessionState
    {
        std::vector<std::string> recentFiles;
        std::vector<std::string> openFiles;
        int activeOpenFileIndex = -1;
    };

    class RecentFilesPersistence
    {
    public:
        static constexpr std::size_t kMaxEntries = 10;

        static PersistedSessionState load(const std::filesystem::path &path);
        static bool save(const std::filesystem::path &path,
                         const PersistedSessionState &state);
    };
} // namespace cupuacu::persistence
