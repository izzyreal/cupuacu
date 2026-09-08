#pragma once
#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace cupuacu::concurrency
{
    // Bound resources belong in Process, not Request. Only the worker owns
    // Process after construction, including its final resource release.
    template <typename Request, typename Value> class LatestValueWorker
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
        explicit LatestValueWorker(Process process)
            : state(std::make_shared<State>())
        {
            std::thread(
                [shared = state, process = std::move(process)]() mutable
                {
                    run(*shared, process);
                    process = {}; // Release readers/files before signaling
                                  // completion.
                    {
                        std::lock_guard lock(shared->mutex);
                        shared->finished = true;
                    }
                    shared->cv.notify_all();
                })
                .detach();
        }
        ~LatestValueWorker()
        {
            close();
        }
        LatestValueWorker(const LatestValueWorker &) = delete;
        LatestValueWorker &operator=(const LatestValueWorker &) = delete;
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
            state->cv.notify_one();
            return generation;
        }
        std::optional<Result> takePublished()
        {
            std::lock_guard lock(state->mutex);
            return std::exchange(state->published, std::nullopt);
        }
        void close()
        {
            std::lock_guard lock(state->mutex);
            state->closed = true;
            state->pending.reset();
            state->published.reset();
            state->cv.notify_all();
        }
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
                               return state->finished;
                           });
        }

    private:
        struct State
        {
            std::mutex mutex;
            std::condition_variable cv;
            bool closed = false, finished = false;
            uint64_t generation = 0;
            std::optional<Request> pending;
            std::optional<Result> published;
        };
        static void run(State &state, const Process &process)
        {
            for (;;)
            {
                Request request;
                uint64_t generation;
                {
                    std::unique_lock lock(state.mutex);
                    state.cv.wait(lock,
                                  [&]
                                  {
                                      return state.closed ||
                                             state.pending.has_value();
                                  });
                    if (state.closed)
                    {
                        return;
                    }
                    request = std::move(*state.pending);
                    state.pending.reset();
                    generation = state.generation;
                }
                const CancelCheck canceled = [&]
                {
                    std::lock_guard lock(state.mutex);
                    return state.closed || state.generation != generation;
                };
                Result result{generation, {}, {}};
                try
                {
                    if (canceled())
                    {
                        continue;
                    }
                    result.value = process(request, canceled);
                    if (!result.value)
                    {
                        continue;
                    }
                }
                catch (...)
                {
                    result.error = std::current_exception();
                }
                std::lock_guard lock(state.mutex);
                if (!state.closed && state.generation == generation)
                {
                    state.published = std::move(result);
                }
            }
        }
        std::shared_ptr<State> state;
    };
} // namespace cupuacu::concurrency
