#include "persistence/SessionStatePersistence.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>

namespace cupuacu::persistence
{
    namespace
    {
        constexpr int kFormatVersion = 4;

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

        bool loadOptionalInt64(const nlohmann::json &json, const char *key,
                               std::optional<int64_t> &value)
        {
            if (!json.contains(key))
            {
                return true;
            }
            if (!json.at(key).is_number_integer())
            {
                return false;
            }
            value = json.at(key).get<int64_t>();
            return true;
        }

        bool loadOptionalDouble(const nlohmann::json &json, const char *key,
                                std::optional<double> &value)
        {
            if (!json.contains(key))
            {
                return true;
            }
            if (!json.at(key).is_number())
            {
                return false;
            }
            value = json.at(key).get<double>();
            return true;
        }

        bool loadMarkers(const nlohmann::json &json,
                         std::vector<PersistedDocumentMarker> &markers)
        {
            markers.clear();
            if (!json.contains("markers"))
            {
                return true;
            }
            if (!json.at("markers").is_array())
            {
                return false;
            }

            for (const auto &entry : json.at("markers"))
            {
                if (!entry.is_object() || !entry.contains("frame") ||
                    !entry.at("frame").is_number_integer())
                {
                    return false;
                }

                PersistedDocumentMarker marker{};
                marker.frame = entry.at("frame").get<int64_t>();

                if (entry.contains("id"))
                {
                    if (!entry.at("id").is_number_unsigned())
                    {
                        return false;
                    }
                    marker.id = entry.at("id").get<uint64_t>();
                }

                if (entry.contains("label"))
                {
                    if (!entry.at("label").is_string())
                    {
                        return false;
                    }
                    marker.label = entry.at("label").get<std::string>();
                }

                markers.push_back(std::move(marker));
            }

            return true;
        }

        std::optional<std::vector<PersistedOpenDocumentState>> loadOpenDocuments(
            const nlohmann::json &json)
        {
            if (!json.contains("openDocuments") ||
                !json.at("openDocuments").is_array())
            {
                return std::nullopt;
            }

            std::vector<PersistedOpenDocumentState> result;
            for (const auto &entry : json.at("openDocuments"))
            {
                if (!entry.is_object() || !entry.contains("filePath") ||
                    !entry.at("filePath").is_string())
                {
                    return std::nullopt;
                }

                PersistedOpenDocumentState documentState{};
                documentState.filePath = entry.at("filePath").get<std::string>();
                if (documentState.filePath.empty())
                {
                    continue;
                }
                if (!loadOptionalDouble(entry, "samplesPerPixel",
                                        documentState.samplesPerPixel) ||
                    !loadOptionalInt64(entry, "sampleOffset",
                                       documentState.sampleOffset) ||
                    !loadOptionalInt64(entry, "cursor",
                                       documentState.cursor) ||
                    !loadOptionalInt64(entry, "selectionStart",
                                       documentState.selectionStart) ||
                    !loadOptionalInt64(entry, "selectionEndExclusive",
                                       documentState.selectionEndExclusive) ||
                    !loadMarkers(entry, documentState.markers))
                {
                    return std::nullopt;
                }

                result.push_back(std::move(documentState));
            }

            return result;
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

        if (!json.is_object())
        {
            return {};
        }

        const int version = json.value("version", 0);
        if (version != 1 && version != 2 && version != 3 &&
            version != kFormatVersion)
        {
            return {};
        }

        if (version >= 2)
        {
            const auto openDocuments = loadOpenDocuments(json);
            if (!openDocuments.has_value())
            {
                return {};
            }
            state.openDocuments = *openDocuments;
            state.openFiles.reserve(state.openDocuments.size());
            for (const auto &documentState : state.openDocuments)
            {
                state.openFiles.push_back(documentState.filePath);
            }
        }
        else
        {
            const auto openFiles = loadStringArray(json, "openFiles");
            if (!openFiles.has_value())
            {
                return {};
            }

            state.openFiles = *openFiles;
            state.openDocuments.reserve(state.openFiles.size());
            for (const auto &pathString : state.openFiles)
            {
                state.openDocuments.push_back(PersistedOpenDocumentState{
                    .filePath = pathString,
                });
            }
        }

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

        std::vector<PersistedOpenDocumentState> openDocuments;
        openDocuments.reserve(state.openDocuments.size());
        for (const auto &documentState : state.openDocuments)
        {
            if (!documentState.filePath.empty())
            {
                openDocuments.push_back(documentState);
            }
        }

        if (openDocuments.empty())
        {
            for (const auto &pathString : filterFiles(state.openFiles))
            {
                openDocuments.push_back(PersistedOpenDocumentState{
                    .filePath = pathString,
                });
            }
        }

        std::vector<std::string> openFiles;
        openFiles.reserve(openDocuments.size());
        for (const auto &documentState : openDocuments)
        {
            openFiles.push_back(documentState.filePath);
        }
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

        nlohmann::json openDocumentsJson = nlohmann::json::array();
        for (const auto &documentState : openDocuments)
        {
            nlohmann::json entry{
                {"filePath", documentState.filePath},
            };
            if (documentState.samplesPerPixel.has_value())
            {
                entry["samplesPerPixel"] = *documentState.samplesPerPixel;
            }
            if (documentState.sampleOffset.has_value())
            {
                entry["sampleOffset"] = *documentState.sampleOffset;
            }
            if (documentState.cursor.has_value())
            {
                entry["cursor"] = *documentState.cursor;
            }
            if (documentState.selectionStart.has_value())
            {
                entry["selectionStart"] = *documentState.selectionStart;
            }
            if (documentState.selectionEndExclusive.has_value())
            {
                entry["selectionEndExclusive"] =
                    *documentState.selectionEndExclusive;
            }
            if (!documentState.markers.empty())
            {
                nlohmann::json markersJson = nlohmann::json::array();
                for (const auto &marker : documentState.markers)
                {
                    nlohmann::json markerJson{
                        {"frame", marker.frame},
                    };
                    if (marker.id != 0)
                    {
                        markerJson["id"] = marker.id;
                    }
                    if (!marker.label.empty())
                    {
                        markerJson["label"] = marker.label;
                    }
                    markersJson.push_back(std::move(markerJson));
                }
                entry["markers"] = std::move(markersJson);
            }
            openDocumentsJson.push_back(std::move(entry));
        }

        const nlohmann::json json{
            {"version", kFormatVersion},
            {"openDocuments", openDocumentsJson},
            {"activeOpenFileIndex", activeOpenFileIndex},
        };

        output << json.dump(2) << '\n';
        return output.good();
    }
} // namespace cupuacu::persistence
