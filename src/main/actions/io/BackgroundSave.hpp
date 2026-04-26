#pragma once

#include "../../State.hpp"
#include "../../Document.hpp"
#include "../../file/AudioExport.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

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

    bool queueOverwrite(cupuacu::State *state);
    bool queueOverwritePreserving(cupuacu::State *state);
    bool queueSaveAs(cupuacu::State *state, const std::string &absoluteFilePath,
                     const file::AudioExportSettings &settings);
    bool queueSaveAsPreserving(cupuacu::State *state,
                               const std::string &absoluteFilePath,
                               const file::AudioExportSettings &settings);
    void processPendingSaveWork(cupuacu::State *state);
} // namespace cupuacu::actions::io
