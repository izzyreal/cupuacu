#include "persistence/RecentFilesPersistence.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace cupuacu::persistence
{
    namespace
    {
        constexpr int kFormatVersion = 1;

        std::vector<std::string> clampFiles(const std::vector<std::string> &files)
        {
            std::vector<std::string> result;
            result.reserve(std::min(files.size(),
                                    RecentFilesPersistence::kMaxEntries));

            for (const auto &file : files)
            {
                if (file.empty())
                {
                    continue;
                }
                result.push_back(file);
                if (result.size() >= RecentFilesPersistence::kMaxEntries)
                {
                    break;
                }
            }

            return result;
        }
    } // namespace

    std::vector<std::string> RecentFilesPersistence::load(
        const std::filesystem::path &path)
    {
        if (path.empty() || !std::filesystem::exists(path))
        {
            return {};
        }

        std::ifstream input(path);
        if (!input.is_open())
        {
            return {};
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

        if (!json.is_object() || json.value("version", 0) != kFormatVersion ||
            !json.contains("files") || !json.at("files").is_array())
        {
            return {};
        }

        std::vector<std::string> files;
        for (const auto &entry : json.at("files"))
        {
            if (!entry.is_string())
            {
                return {};
            }
            files.push_back(entry.get<std::string>());
        }

        return clampFiles(files);
    }

    bool RecentFilesPersistence::save(const std::filesystem::path &path,
                                      const std::vector<std::string> &files)
    {
        if (path.empty())
        {
            return false;
        }

        const auto trimmed = clampFiles(files);
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
            {"files", trimmed},
        };

        output << json.dump(2) << '\n';
        return output.good();
    }
} // namespace cupuacu::persistence
