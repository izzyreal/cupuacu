#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace cupuacu::concurrency
{
    // A lazy single worker with bounded pending work. Saturation leaves the
    // caller free to retry later. Destruction drains accepted requests.
    template <typename Request, std::size_t MaxPending>
    class BoundedBackgroundWorker
    {
        static_assert(MaxPending > 0);

    public:
        explicit BoundedBackgroundWorker(
            std::function<void(const Request &)> process)
            : process(std::move(process))
        {
        }

        ~BoundedBackgroundWorker()
        {
            {
                std::lock_guard lock(mutex);
                stopping = true;
            }
            cv.notify_all();
            if (worker.joinable())
            {
                worker.join();
            }
        }

        bool schedule(Request request)
        {
            std::lock_guard lock(mutex);
            if (pending.size() >= MaxPending)
            {
                return false;
            }
            if (!worker.joinable())
            {
                worker = std::thread(
                    [this]
                    {
                        run();
                    });
            }
            pending.push_back(std::move(request));
            cv.notify_all();
            return true;
        }

        bool hasWork()
        {
            std::lock_guard lock(mutex);
            return active || !pending.empty();
        }

        void flush()
        {
            std::unique_lock lock(mutex);
            cv.wait(lock,
                    [this]
                    {
                        return !active && pending.empty();
                    });
        }

    private:
        std::function<void(const Request &)> process;
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<Request> pending;
        bool active = false;
        bool stopping = false;
        std::thread worker;

        void run()
        {
            for (;;)
            {
                {
                    std::unique_lock lock(mutex);
                    cv.wait(lock,
                            [this]
                            {
                                return stopping || !pending.empty();
                            });
                    if (pending.empty())
                    {
                        return;
                    }
                    auto request = std::move(pending.front());
                    pending.pop_front();
                    active = true;
                    lock.unlock();
                    // Handlers must contain their own failure handling.
                    process(request);
                    // Release retained data on this worker before reporting
                    // idle.
                }
                {
                    std::lock_guard lock(mutex);
                    active = false;
                }
                cv.notify_all();
            }
        }
    };
} // namespace cupuacu::concurrency
