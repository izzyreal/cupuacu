#pragma once
#include <chrono>
#include <cstdint>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace cupuacu::concurrency
{
    // Bulk work only. Transport and latest-viewport services retain independent
    // capacity, so a blocked import cannot occupy their workers.
    class TaskScheduler
    {
    public:
        enum class Priority
        {
            User,
            Autosave,
            Maintenance
        };
        struct Options
        {
            Priority priority = Priority::User;
            uint64_t scratchBytes = 0;
            uint64_t documentId = 0;
            std::chrono::steady_clock::time_point deadline{};
        };
        struct Stats
        {
            std::size_t queued = 0, running = 0, peakRunning = 0;
            uint64_t reservedBytes = 0, peakReservedBytes = 0;
        };
        struct Ticket
        {
            std::shared_future<void> completion;
            std::shared_ptr<void> admission;
            bool valid() const
            {
                return completion.valid();
            }
            void wait() const
            {
                completion.wait();
            }
            void get() const
            {
                completion.get();
            }
        };
        explicit TaskScheduler(std::size_t workers = 2,
                               std::size_t queueLimit = 64,
                               uint64_t scratchBudget = 128 * 1024 * 1024);
        ~TaskScheduler();
        Ticket submit(std::function<void()> work, Options options);
        Stats stats() const;

    private:
        struct Entry
        {
            std::packaged_task<void()> work;
            Options options;
            std::shared_ptr<void> admission;
        };
        std::shared_ptr<std::atomic<std::size_t>> outstanding =
            std::make_shared<std::atomic<std::size_t>>(0);
        mutable std::mutex mutex;
        std::condition_variable cv;
        std::deque<Entry> queue;
        std::vector<std::thread> workers;
        std::size_t workerCount, queueLimit;
        uint64_t scratchBudget;
        Stats counters;
        bool stopping = false;
        void run();
    };
    // For direct worker-service callers without an application State.
    std::shared_ptr<TaskScheduler> defaultTaskScheduler();
} // namespace cupuacu::concurrency
