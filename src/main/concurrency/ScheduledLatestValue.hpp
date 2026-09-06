#pragma once
#include "TaskScheduler.hpp"
#include "DeferredRelease.hpp"
#include <optional>
#include <utility>

namespace cupuacu::concurrency
{
    // Coalescing bulk service: one queued/running task and one publication.
    // Each request relinquishes its scheduler slot before the next is queued.
    // Use only for replaceable requests, never accepted document mutations.
    template <typename Request, typename Value> class ScheduledLatestValue
    {
    public:
        using CancelCheck = std::function<bool()>;
        using Process = std::function<std::optional<Value>(
            const Request &, const CancelCheck &)>;
        struct Result
        {
            uint64_t generation = 0;
            std::optional<Value> value;
            std::exception_ptr error;
        };
        explicit ScheduledLatestValue(
            Process process, std::shared_ptr<TaskScheduler> scheduler = {},
            TaskScheduler::Options options = {})
            : state(releaseOnWorker(std::make_shared<State>()))
        {
            state->process = std::move(process);
            state->scheduler =
                scheduler ? std::move(scheduler) : defaultTaskScheduler();
            state->options = std::move(options);
        }
        ~ScheduledLatestValue()
        {
            close();
        }
        ScheduledLatestValue(const ScheduledLatestValue &) = delete;
        ScheduledLatestValue &operator=(const ScheduledLatestValue &) = delete;
        uint64_t submit(Request request)
        {
            std::lock_guard lock(state->mutex);
            if (state->closed)
            {
                throw std::logic_error("Worker is closed");
            }
            state->pending = std::move(request);
            state->published.reset();
            const auto generation = ++state->generation;
            if (!state->scheduled)
            {
                schedule(state);
            }
            return generation;
        }
        std::optional<Result> takePublished()
        {
            std::lock_guard lock(state->mutex);
            auto result = std::exchange(state->published, std::nullopt);
            if (!state->scheduled)
            {
                state->admission.reset();
            }
            return result;
        }
        void close()
        {
            std::lock_guard lock(state->mutex);
            state->closed = true;
            state->pending.reset();
            state->published.reset();
            state->cv.notify_all();
        }
        // Test/shutdown helper; application event handlers never wait here.
        void waitUntilClosed()
        {
            std::unique_lock lock(state->mutex);
            if (!state->closed)
            {
                throw std::logic_error("Close worker before waiting");
            }
            state->cv.wait(lock,
                           [&]
                           {
                               return !state->scheduled;
                           });
        }

    private:
        struct State
        {
            std::mutex mutex;
            std::condition_variable cv;
            bool closed = false, scheduled = false;
            uint64_t generation = 0;
            std::optional<Request> pending;
            std::optional<Result> published;
            Process process;
            std::shared_ptr<TaskScheduler> scheduler;
            TaskScheduler::Options options;
            std::shared_ptr<void> admission;
        };
        static void schedule(const std::shared_ptr<State> &s)
        {
            s->scheduled = true;
            try
            {
                // Do not retain the future: its callable owns State. Retain
                // only admission, including while a result awaits publication.
                auto options = s->options;
                options.admissionFailed = [s](std::exception_ptr error)
                {
                    std::lock_guard lock(s->mutex);
                    if (!s->closed)
                    {
                        s->published = Result{s->generation, {}, error};
                    }
                    s->pending.reset();
                    s->scheduled = false;
                    s->cv.notify_all();
                };
                s->admission = s->scheduler
                                   ->submit(
                                       [s]
                                       {
                                           run(s);
                                       },
                                       std::move(options))
                                   .admission;
            }
            catch (...)
            {
                s->published =
                    Result{s->generation, {}, std::current_exception()};
                s->pending.reset();
                s->scheduled = false;
                s->cv.notify_all();
            }
        }
        static void run(const std::shared_ptr<State> &s)
        {
            std::optional<Request> request;
            uint64_t generation;
            {
                std::lock_guard lock(s->mutex);
                if (!s->closed)
                {
                    request = std::move(s->pending);
                }
                s->pending.reset();
                generation = s->generation;
            }
            Result result{generation, {}, {}};
            const CancelCheck cancel = [s, generation]
            {
                std::lock_guard lock(s->mutex);
                return s->closed || s->generation != generation;
            };
            try
            {
                if (request && !cancel())
                {
                    result.value = s->process(*request, cancel);
                }
            }
            catch (...)
            {
                result.error = std::current_exception();
            }
            std::lock_guard lock(s->mutex);
            if (!s->closed && generation == s->generation)
            {
                s->published = std::move(result);
            }
            s->scheduled = false;
            if (!s->closed && s->pending)
            {
                schedule(s);
            }
            s->cv.notify_all();
        }
        std::shared_ptr<State> state;
    };
} // namespace cupuacu::concurrency
