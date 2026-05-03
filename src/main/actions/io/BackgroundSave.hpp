#pragma once

#include "../../State.hpp"
#include "../../Document.hpp"
#include "../../file/AudioExport.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace cupuacu::actions::io
{
    enum class BackgroundSaveKind
    {
        Overwrite,
        OverwritePreserving,
        SaveAs,
        SaveAsPreserving,
    };

    struct BackgroundSaveRequest
    {
        BackgroundSaveKind kind = BackgroundSaveKind::SaveAs;
        std::filesystem::path path;
        std::filesystem::path referencePath;
        file::AudioExportSettings settings;
    };

    class BackgroundSaveJob
    {
    public:
        struct Snapshot
        {
            bool completed = false;
            bool success = false;
            BackgroundSaveRequest request;
            std::string detail;
            std::optional<double> progress;
            std::string error;
        };

        BackgroundSaveJob(std::uint64_t idToUse,
                          BackgroundSaveRequest requestToSave,
                          cupuacu::State *stateToUse,
                          const cupuacu::Document *documentToWrite);
        ~BackgroundSaveJob();

        BackgroundSaveJob(const BackgroundSaveJob &) = delete;
        BackgroundSaveJob &operator=(const BackgroundSaveJob &) = delete;

        void start();
        [[nodiscard]] Snapshot snapshot() const;
        [[nodiscard]] std::uint64_t getId() const;

    private:
        std::uint64_t id = 0;
        BackgroundSaveRequest request;
        cupuacu::State *state = nullptr;
        const cupuacu::Document *document = nullptr;
        mutable std::mutex mutex;
        bool completed = false;
        bool success = false;
        std::string detail;
        std::optional<double> progress;
        std::string error;
        std::thread worker;

        void run();
        void publishProgress(const std::string &detailToUse,
                             std::optional<double> progressToUse);
    };

    class BackgroundAutosaveJob
    {
    public:
        struct Snapshot
        {
            bool completed = false;
            bool success = false;
            uint64_t tabId = 0;
            std::filesystem::path path;
            uint64_t waveformDataVersion = 0;
            uint64_t markerDataVersion = 0;
            std::string currentFile;
            std::optional<double> progress;
            std::string error;
        };

        BackgroundAutosaveJob(uint64_t tabIdToUse,
                              std::filesystem::path pathToUse,
                              uint64_t waveformDataVersionToUse,
                              uint64_t markerDataVersionToUse,
                              std::string currentFileToUse);

        [[nodiscard]] Snapshot snapshot() const;
        void pump(cupuacu::State *state);

    private:
        struct MarkerSnapshot
        {
            uint64_t id = 0;
            int64_t frame = 0;
            std::string label;
        };

        uint64_t tabId = 0;
        std::filesystem::path path;
        uint64_t waveformDataVersion = 0;
        uint64_t markerDataVersion = 0;
        std::string currentFile;
        std::vector<MarkerSnapshot> markers;
        bool initialized = false;
        bool completed = false;
        bool success = false;
        std::string error;
        int sampleFormat = 0;
        int sampleRate = 0;
        int64_t channelCount = 0;
        int64_t frameCount = 0;
        int64_t nextFrameToWrite = 0;
        std::filesystem::path temporaryPath;
        std::ofstream output;

        void initializeFromSession(const cupuacu::DocumentSession &session);
        void writeFrameChunk(const cupuacu::Document::ReadLease &lease,
                             int64_t frameStart, int64_t frameCountToWrite);
        void finish();
        void fail(std::string message);
        [[nodiscard]] double progressValue() const;
    };

    bool queueOverwrite(cupuacu::State *state);
    bool queueOverwritePreserving(cupuacu::State *state);
    bool queueSaveAs(cupuacu::State *state, const std::string &absoluteFilePath,
                     const file::AudioExportSettings &settings);
    bool queueSaveAsPreserving(cupuacu::State *state,
                               const std::string &absoluteFilePath,
                               const file::AudioExportSettings &settings);
    void queueAutosaveForTab(cupuacu::State *state, int tabIndex);
    void processPendingSaveWork(cupuacu::State *state);
    void processPendingAutosaveWork(cupuacu::State *state);
} // namespace cupuacu::actions::io
