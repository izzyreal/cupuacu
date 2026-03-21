#include "persistence/RecentFilesPersistence.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <algorithm>

namespace cupuacu::persistence
{
    namespace
    {
        constexpr int kLegacyFormatVersion = 1;
        constexpr int kFormatVersion = 2;

        std::vector<std::string> filterFiles(const std::vector<std::string> &files,
                                             const std::size_t maxEntries)
        {
            std::vector<std::string> result;
            if (maxEntries > 0)
            {
                result.reserve(std::min(files.size(), maxEntries));
            }
            else
            {
                result.reserve(files.size());
            }

            for (const auto &file : files)
            {
                if (file.empty())
                {
                    continue;
                }
                result.push_back(file);
                if (maxEntries > 0 && result.size() >= maxEntries)
                {
                    break;
                }
            }

            return result;
        }

        std::optional<std::vector<std::string>> loadStringArray(
            const nlohmann::json &json, const char *key,
            const std::size_t maxEntries)
        {
            if (!json.contains(key) || !json.at(key).is_array())
            {
                return std::nullopt;
            }

            std::vector<std::string> files;
            for (const auto &entry : json.at(key))
            {
                if (!entry.is_string())
                {
                    return std::nullopt;
                }
                files.push_back(entry.get<std::string>());
            }

            return filterFiles(files, maxEntries);
        }
    } // namespace

    PersistedSessionState RecentFilesPersistence::load(
        const std::filesystem::path &path)
    {
        PersistedSessionState state{};

        if (path.empty() || !std::filesystem::exists(path))
        {
            return state;
        }

        std::ifstream input(path);
        if (!input.is_open())
        {
            return state;
        }

        nlohmann::json json;
        try
        {
            input >> json;
        }
        catch (...)
        {
            return state;
        }

        if (!json.is_object())
        {
            return state;
        }

        const int version = json.value("version", 0);
        if (version == kLegacyFormatVersion)
        {
            const auto files = loadStringArray(
                json, "files", RecentFilesPersistence::kMaxEntries);
            if (!files.has_value())
            {
                return {};
            }
            state.recentFiles = *files;
            return state;
        }

        if (version != kFormatVersion)
        {
            return state;
        }

        const auto recentFiles = loadStringArray(
            json, "recentFiles", RecentFilesPersistence::kMaxEntries);
        const auto openFiles = loadStringArray(json, "openFiles", 0);
        if (!recentFiles.has_value() || !openFiles.has_value())
        {
            return {};
        }

        state.recentFiles = *recentFiles;
        state.openFiles = *openFiles;
        if (json.contains("activeOpenFileIndex") &&
            json.at("activeOpenFileIndex").is_number_integer())
        {
            state.activeOpenFileIndex =
                json.at("activeOpenFileIndex").get<int>();
        }
        return state;
    }

    bool RecentFilesPersistence::save(const std::filesystem::path &path,
                                      const PersistedSessionState &state)
    {
        if (path.empty())
        {
            return false;
        }

        const auto trimmedRecentFiles = filterFiles(
            state.recentFiles, RecentFilesPersistence::kMaxEntries);
        const auto trimmedOpenFiles = filterFiles(state.openFiles, 0);

        int activeOpenFileIndex = state.activeOpenFileIndex;
        if (trimmedOpenFiles.empty())
        {
            activeOpenFileIndex = -1;
        }
        else
        {
            activeOpenFileIndex =
                std::clamp(activeOpenFileIndex, 0,
                           static_cast<int>(trimmedOpenFiles.size()) - 1);
        }

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            return false;
        }

        std::ofstream output(path);
        if (!output.is_open())
        {
            return false;
        }

        const nlohmann::json json{
            {"version", kFormatVersion},
            {"recentFiles", trimmedRecentFiles},
            {"openFiles", trimmedOpenFiles},
            {"activeOpenFileIndex", activeOpenFileIndex},
        };

        output << json.dump(2) << '\n';
        return output.good();
    }
} // namespace cupuacu::persistence
