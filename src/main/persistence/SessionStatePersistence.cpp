#include "persistence/SessionStatePersistence.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>

namespace cupuacu::persistence
{
    namespace
    {
        constexpr int kFormatVersion = 1;

        std::vector<std::string> filterFiles(const std::vector<std::string> &files)
        {
            std::vector<std::string> result;
            result.reserve(files.size());
            for (const auto &file : files)
            {
                if (!file.empty())
                {
                    result.push_back(file);
                }
            }
            return result;
        }

        std::optional<std::vector<std::string>> loadStringArray(
            const nlohmann::json &json, const char *key)
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

            return filterFiles(files);
        }
    } // namespace

    PersistedSessionState SessionStatePersistence::load(
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
            return {};
        }

        if (!json.is_object() || json.value("version", 0) != kFormatVersion)
        {
            return {};
        }

        const auto openFiles = loadStringArray(json, "openFiles");
        if (!openFiles.has_value())
        {
            return {};
        }

        state.openFiles = *openFiles;
        if (json.contains("activeOpenFileIndex") &&
            json.at("activeOpenFileIndex").is_number_integer())
        {
            state.activeOpenFileIndex =
                json.at("activeOpenFileIndex").get<int>();
        }
        return state;
    }

    bool SessionStatePersistence::save(const std::filesystem::path &path,
                                       const PersistedSessionState &state)
    {
        if (path.empty())
        {
            return false;
        }

        const auto openFiles = filterFiles(state.openFiles);
        const int activeOpenFileIndex =
            openFiles.empty()
                ? -1
                : std::clamp(state.activeOpenFileIndex, 0,
                             static_cast<int>(openFiles.size()) - 1);

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
            {"openFiles", openFiles},
            {"activeOpenFileIndex", activeOpenFileIndex},
        };

        output << json.dump(2) << '\n';
        return output.good();
    }
} // namespace cupuacu::persistence
