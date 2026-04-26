#pragma once

#include "../../State.hpp"
#include "../../file/file_loading.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace cupuacu::actions::io
{
    class BackgroundOpenJob
    {
    public:
        struct Snapshot
        {
            bool completed = false;
            bool success = false;
            PendingOpenRequest request;
            std::string path;
            std::string detail;
            std::optional<double> progress;
            std::string error;
        };

        BackgroundOpenJob(std::uint64_t idToUse,
                          PendingOpenRequest requestToOpen);
        ~BackgroundOpenJob();

        BackgroundOpenJob(const BackgroundOpenJob &) = delete;
        BackgroundOpenJob &operator=(const BackgroundOpenJob &) = delete;

        void start();
        [[nodiscard]] Snapshot snapshot() const;
        [[nodiscard]] std::unique_ptr<file::LoadedAudioFile> takeLoadedFile();
        [[nodiscard]] std::uint64_t getId() const;
        [[nodiscard]] const std::string &getPath() const;
        [[nodiscard]] const PendingOpenRequest &getRequest() const;

    private:
        std::uint64_t id = 0;
        PendingOpenRequest request;
        mutable std::mutex mutex;
        bool completed = false;
        bool success = false;
        std::string detail;
        std::optional<double> progress;
        std::string error;
        std::unique_ptr<file::LoadedAudioFile> loadedFile;
        std::thread worker;

        void run();
        void publishProgress(const std::string &detailToUse,
                             std::optional<double> progressToUse);
    };

    void queueOpenFile(cupuacu::State *state, std::string path);
    void queueOpenRequest(cupuacu::State *state, PendingOpenRequest request);
    void processPendingOpenWork(cupuacu::State *state);
} // namespace cupuacu::actions::io
