#pragma once

#include "../../State.hpp"
#include "../../Document.hpp"
#include "../../file/AudioExport.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <atomic>
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
            bool canceled = false;
            bool persistentWaveformCacheSaved = false;
            BackgroundSaveRequest request;
            std::string detail;
            std::optional<double> progress;
            std::string error;
        };

        BackgroundSaveJob(std::uint64_t idToUse,
                          BackgroundSaveRequest requestToSave,
                          cupuacu::State *stateToUse,
                          const cupuacu::Document &documentToWrite,
                          std::filesystem::path waveformCacheRootToUse = {});
        ~BackgroundSaveJob();

        BackgroundSaveJob(const BackgroundSaveJob &) = delete;
        BackgroundSaveJob &operator=(const BackgroundSaveJob &) = delete;

        void start();
        [[nodiscard]] Snapshot snapshot() const;
        [[nodiscard]] std::uint64_t getId() const;
        void cancel();

    private:
        std::uint64_t id = 0;
        BackgroundSaveRequest request;
        cupuacu::State *state = nullptr;
        cupuacu::Document document;
        std::filesystem::path waveformCacheRoot;
        mutable std::mutex mutex;
        bool completed = false;
        bool success = false;
        bool persistentWaveformCacheSaved = false;
        std::string detail;
        std::optional<double> progress;
        std::string error;
        std::thread worker;
        std::atomic<bool> cancelRequested{false};

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

        BackgroundAutosaveJob(
            uint64_t tabIdToUse, std::filesystem::path pathToUse,
            uint64_t waveformDataVersionToUse, uint64_t markerDataVersionToUse,
            std::string currentFileToUse,
            const cupuacu::Document &documentToSave,
            const waveform::DocumentWaveformCaches &cachesToSave);
        ~BackgroundAutosaveJob();

        BackgroundAutosaveJob(const BackgroundAutosaveJob &) = delete;
        BackgroundAutosaveJob &
        operator=(const BackgroundAutosaveJob &) = delete;

        void start();
        [[nodiscard]] Snapshot snapshot() const;

    private:
        uint64_t tabId = 0;
        std::filesystem::path path;
        uint64_t waveformDataVersion = 0;
        uint64_t markerDataVersion = 0;
        std::string currentFile;
        cupuacu::Document document;
        waveform::DocumentWaveformCaches waveformCaches;
        mutable std::mutex mutex;
        bool completed = false;
        bool success = false;
        std::string error;
        std::thread worker;

        void run();
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
