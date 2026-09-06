#pragma once

#include "AudioEditRevision.hpp"
#include "../audio/RecordedChunk.hpp"
#include <atomic>
#include <mutex>
#include <thread>

namespace cupuacu::storage
{
    // A single producer hands off callback chunks without allocation or I/O.
    // Only the worker writes blocks and constructs waveform summaries.
    class RecordingWriter
    {
    public:
        static constexpr std::size_t queueChunks = 512;
        static constexpr int64_t batchFrames = 8192;
        struct Snapshot
        {
            std::shared_ptr<const AudioEditRevision> audio;
            int64_t endFrame = 0;
            bool completed = false;
            std::string error;
            uint64_t sampleBytesWritten = 0;
        };
        RecordingWriter(std::shared_ptr<const AudioEditRevision> before,
                        int64_t start, std::filesystem::path directory);
        ~RecordingWriter();
        void startWorker();
        // false means stop capture; no later chunks may be accepted after a
        // gap.
        bool submit(const audio::RecordedChunk &) noexcept;
        void finish() noexcept;
        void cancel() noexcept;
        Snapshot snapshot() const;
        uint64_t queuedChunks() const
        {
            return head.load() - tail.load();
        }
        uint64_t peakQueuedChunks() const
        {
            return highWater.load();
        }

    private:
        std::shared_ptr<const AudioEditRevision> original;
        int64_t start;
        std::filesystem::path directory;
        std::array<audio::RecordedChunk, queueChunks> queue;
        std::atomic<uint64_t> head{0}, tail{0}, highWater{0};
        std::atomic_bool finishing{false}, canceled{false}, overflow{false};
        mutable std::mutex publicationMutex;
        Snapshot publication;
        std::thread worker;
        void run() noexcept;
    };
} // namespace cupuacu::storage
