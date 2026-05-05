#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cupuacu::persistence
{
    struct PersistedDocumentMarker
    {
        uint64_t id = 0;
        int64_t frame = 0;
        std::string label;
    };

    struct PersistedOpenDocumentState
    {
        std::string filePath;
        std::string autosaveSnapshotPath;
        std::string undoStorePath;
        std::optional<double> samplesPerPixel;
        std::optional<int64_t> sampleOffset;
        std::optional<int64_t> cursor;
        std::optional<int64_t> selectionStart;
        std::optional<int64_t> selectionEndExclusive;
        std::vector<PersistedDocumentMarker> markers;
    };

    struct PersistedSessionState
    {
        std::vector<PersistedOpenDocumentState> openDocuments;
        std::vector<std::string> openFiles;
        int activeOpenFileIndex = -1;
        bool snapEnabled = false;
        std::optional<int> windowWidth;
        std::optional<int> windowHeight;
        std::optional<int> windowX;
        std::optional<int> windowY;
    };

    class SessionStatePersistence
    {
    public:
        static PersistedSessionState load(const std::filesystem::path &path);
        static bool save(const std::filesystem::path &path,
                         const PersistedSessionState &state);
    };
} // namespace cupuacu::persistence
