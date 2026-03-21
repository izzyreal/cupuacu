#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace cupuacu::persistence
{
    class RecentFilesPersistence
    {
    public:
        static constexpr std::size_t kMaxEntries = 10;

        static std::vector<std::string> load(const std::filesystem::path &path);
        static bool save(const std::filesystem::path &path,
                         const std::vector<std::string> &recentFiles);
    };
} // namespace cupuacu::persistence
