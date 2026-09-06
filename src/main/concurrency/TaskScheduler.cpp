#include "TaskScheduler.hpp"
#include "DeferredRelease.hpp"
#include "../storage/AudioBlockStore.hpp"
#include <algorithm>
#include <stdexcept>
namespace cupuacu::concurrency
{
    TaskScheduler::TaskScheduler(
        std::size_t count, std::size_t limit, uint64_t budget,
        std::shared_ptr<storage::DecodedBlockCache> memory)
        : workerCount(count), queueLimit(limit), scratchBudget(budget),
          memory(std::move(memory))
    {
        if (!count || !limit)
        {
            throw std::invalid_argument("Invalid task scheduler capacity");
        }
    }
    TaskScheduler::~TaskScheduler()
    {
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        cv.notify_all();
        for (auto &worker : workers)
        {
            worker.join();
        }
    }
    TaskScheduler::Ticket TaskScheduler::submit(std::function<void()> work,
                                                Options options)
    {
        std::lock_guard lock(mutex);
        if (stopping ||
            (queue.size() >= queueLimit ||
             outstanding->load() >= queueLimit + workerCount) ||
            options.scratchBytes > scratchBudget ||
            (memory && options.scratchBytes > memory->byteBudget()))
        {
            throw std::runtime_error("Background task capacity exhausted");
        }
        if (workers.empty())
        {
            for (std::size_t i = 0; i < workerCount; ++i)
            {
                workers.emplace_back(
                    [this]
                    {
                        run();
                    });
            }
        }
        auto admission =
            std::shared_ptr<void>(new char,
                                  [count = outstanding](void *p)
                                  {
                                      delete static_cast<char *>(p);
                                      --*count;
                                  });
        ++*outstanding;
        Entry entry{std::packaged_task<void()>(
                        [work = std::move(work), memory = memory,
                         bytes = options.scratchBytes,
                         admissionFailed = options.admissionFailed]() mutable
                        {
                            // Never reserve while holding the scheduler mutex:
                            // a cache miss must not block UI-side task
                            // submission.
                            std::shared_ptr<void> scratch;
                            try
                            {
                                scratch = memory ? memory->reserveScratch(bytes)
                                                 : nullptr;
                            }
                            catch (...)
                            {
                                if (admissionFailed)
                                {
                                    admissionFailed(std::current_exception());
                                }
                                throw;
                            }
                            work();
                        }),
                    options, admission};
        auto completion = entry.work.get_future().share();
        queue.push_back(std::move(entry));
        counters.queued = queue.size();
        cv.notify_all();
        return {std::move(completion), std::move(admission)};
    }
    TaskScheduler::Stats TaskScheduler::stats() const
    {
        std::lock_guard lock(mutex);
        return counters;
    }
    void TaskScheduler::run()
    {
        for (;;)
        {
            Entry entry;
            {
                std::unique_lock lock(mutex);
                auto eligible = [&]
                {
                    return std::any_of(
                        queue.begin(), queue.end(),
                        [&](const auto &e)
                        {
                            return (!e.options.mutation ||
                                    !e.options.documentId ||
                                    !mutatingDocuments.contains(
                                        e.options.documentId)) &&
                                   e.options.scratchBytes <=
                                       scratchBudget - counters.reservedBytes;
                        });
                };
                cv.wait(lock,
                        [&]
                        {
                            return eligible() || (stopping && queue.empty());
                        });
                if (queue.empty())
                {
                    return;
                }
                const auto now = std::chrono::steady_clock::now();
                auto rank = [&](const Entry &e)
                {
                    return e.options.deadline != std::chrono::steady_clock::
                                                     time_point{} &&
                                   now >= e.options.deadline
                               ? -1
                               : int(e.options.priority);
                };
                auto chosen = queue.end();
                for (auto it = queue.begin(); it != queue.end(); ++it)
                {
                    if ((!it->options.mutation || !it->options.documentId ||
                         !mutatingDocuments.contains(it->options.documentId)) &&
                        it->options.scratchBytes <=
                            scratchBudget - counters.reservedBytes &&
                        (chosen == queue.end() || rank(*it) < rank(*chosen)))
                    {
                        chosen = it;
                    }
                }
                entry = std::move(*chosen);
                queue.erase(chosen);
                counters.queued = queue.size();
                ++counters.running;
                if (entry.options.mutation && entry.options.documentId)
                {
                    mutatingDocuments.insert(entry.options.documentId);
                }
                counters.peakRunning =
                    std::max(counters.peakRunning, counters.running);
                counters.reservedBytes += entry.options.scratchBytes;
                counters.peakReservedBytes = std::max(
                    counters.peakReservedBytes, counters.reservedBytes);
            }
            entry.work(); // packaged_task contains exceptions and signals
                          // waiters.
            {
                std::lock_guard lock(mutex);
                --counters.running;
                if (entry.options.mutation && entry.options.documentId)
                {
                    mutatingDocuments.erase(entry.options.documentId);
                }
                counters.reservedBytes -= entry.options.scratchBytes;
            }
            cv.notify_all();
        }
    }
    std::shared_ptr<TaskScheduler> defaultTaskScheduler()
    {
        static auto scheduler = []
        {
            // Establish the reclaimer first so it outlives scheduler shutdown.
            // Completed tasks may enqueue their final file/revision release.
            auto releaseLifetime = retainForBackgroundRelease({});
            return std::make_shared<TaskScheduler>(
                2, 64, 128 * 1024 * 1024, storage::defaultDecodedBlockCache());
        }();
        return scheduler;
    }
} // namespace cupuacu::concurrency
