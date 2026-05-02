#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace cupuacu::concurrency
{
    template <typename Request, typename PartialResult>
    class LatestWinsBackgroundWorker
    {
    public:
        struct PublishedResult
        {
            std::uint64_t generation = 0;
            PartialResult result{};
        };

        using CancelCheck = std::function<bool()>;
        using PublishFn = std::function<void(PartialResult)>;
        using ProcessFn =
            std::function<void(const Request &, std::uint64_t,
                               const CancelCheck &, const PublishFn &)>;

        explicit LatestWinsBackgroundWorker(ProcessFn processToUse)
            : process(std::move(processToUse)),
              worker([this]
                     { run(); })
        {
        }

        ~LatestWinsBackgroundWorker()
        {
            {
                std::lock_guard lock(mutex);
                stopRequested = true;
                pendingRequest.reset();
            }
            cv.notify_all();
            if (worker.joinable())
            {
                worker.join();
            }
        }

        LatestWinsBackgroundWorker(const LatestWinsBackgroundWorker &) = delete;
        LatestWinsBackgroundWorker &
        operator=(const LatestWinsBackgroundWorker &) = delete;

        [[nodiscard]] std::uint64_t submit(Request request)
        {
            std::lock_guard lock(mutex);
            ++latestRequestedGeneration;
            pendingRequest = std::move(request);
            cv.notify_one();
            return latestRequestedGeneration;
        }

        [[nodiscard]] std::uint64_t latestGeneration() const
        {
            std::lock_guard lock(mutex);
            return latestRequestedGeneration;
        }

        [[nodiscard]] std::vector<PublishedResult> takePublished()
        {
            std::lock_guard lock(mutex);
            std::vector<PublishedResult> drained;
            drained.reserve(published.size());
            while (!published.empty())
            {
                drained.push_back(std::move(published.front()));
                published.pop_front();
            }
            return drained;
        }

    private:
        void run()
        {
            while (true)
            {
                Request request;
                std::uint64_t generation = 0;
                {
                    std::unique_lock lock(mutex);
                    cv.wait(lock, [this]
                            { return stopRequested || pendingRequest.has_value(); });
                    if (stopRequested)
                    {
                        return;
                    }
                    request = std::move(*pendingRequest);
                    pendingRequest.reset();
                    generation = latestRequestedGeneration;
                }

                const CancelCheck isCanceled = [this, generation]()
                {
                    std::lock_guard lock(mutex);
                    return stopRequested || generation != latestRequestedGeneration;
                };
                const PublishFn publish = [this, generation](PartialResult result)
                {
                    std::lock_guard lock(mutex);
                    if (stopRequested || generation != latestRequestedGeneration)
                    {
                        return;
                    }
                    published.push_back(
                        PublishedResult{generation, std::move(result)});
                };

                process(request, generation, isCanceled, publish);
            }
        }

        ProcessFn process;
        mutable std::mutex mutex;
        std::condition_variable cv;
        bool stopRequested = false;
        std::uint64_t latestRequestedGeneration = 0;
        std::optional<Request> pendingRequest;
        std::deque<PublishedResult> published;
        std::thread worker;
    };
} // namespace cupuacu::concurrency
