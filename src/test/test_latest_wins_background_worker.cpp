#include <catch2/catch_test_macros.hpp>

#include "concurrency/LatestWinsBackgroundWorker.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace
{
    template <typename Predicate>
    bool waitUntil(Predicate &&predicate,
                   const std::chrono::milliseconds timeout =
                       std::chrono::milliseconds(500))
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate())
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return predicate();
    }
} // namespace

TEST_CASE("LatestWinsBackgroundWorker publishes progressive results",
          "[concurrency]")
{
    using Worker =
        cupuacu::concurrency::LatestWinsBackgroundWorker<int, int>;

    Worker worker([](const int &request, const std::uint64_t,
                     const Worker::CancelCheck &isCanceled,
                     const Worker::PublishFn &publish)
                  {
                      for (int chunk = 0; chunk < request; ++chunk)
                      {
                          if (isCanceled())
                          {
                              return;
                          }
                          publish(chunk);
                          std::this_thread::sleep_for(
                              std::chrono::milliseconds(1));
                      }
                  });

    const auto generation = worker.submit(4);
    std::vector<Worker::PublishedResult> published;
    REQUIRE(waitUntil([&]
                      {
                          auto drained = worker.takePublished();
                          published.insert(published.end(),
                                           std::make_move_iterator(drained.begin()),
                                           std::make_move_iterator(drained.end()));
                          return published.size() == 4;
                      }));

    REQUIRE(published.size() == 4);
    REQUIRE(published[0].generation == generation);
    REQUIRE(published[1].generation == generation);
    REQUIRE(published[2].generation == generation);
    REQUIRE(published[3].generation == generation);
    REQUIRE(published[0].result == 0);
    REQUIRE(published[1].result == 1);
    REQUIRE(published[2].result == 2);
    REQUIRE(published[3].result == 3);
}

TEST_CASE("LatestWinsBackgroundWorker cancels superseded work and runs latest request",
          "[concurrency]")
{
    using Worker =
        cupuacu::concurrency::LatestWinsBackgroundWorker<int, int>;

    std::atomic<int> canceledChunksSeen{0};
    Worker worker([&](const int &request, const std::uint64_t generation,
                      const Worker::CancelCheck &isCanceled,
                      const Worker::PublishFn &publish)
                  {
                      for (int chunk = 0; chunk < request; ++chunk)
                      {
                          if (isCanceled())
                          {
                              ++canceledChunksSeen;
                              return;
                          }
                          publish(static_cast<int>(generation * 1000) + chunk);
                          std::this_thread::sleep_for(
                              std::chrono::milliseconds(2));
                      }
                  });

    const auto generation1 = worker.submit(100);
    std::vector<Worker::PublishedResult> published;
    REQUIRE(waitUntil([&]
                      {
                          auto drained = worker.takePublished();
                          published.insert(published.end(),
                                           std::make_move_iterator(drained.begin()),
                                           std::make_move_iterator(drained.end()));
                          return !published.empty();
                      }));

    const auto generation2 = worker.submit(3);
    REQUIRE(waitUntil([&]
                      {
                          auto drained = worker.takePublished();
                          published.insert(published.end(),
                                           std::make_move_iterator(drained.begin()),
                                           std::make_move_iterator(drained.end()));
                          int generation2Count = 0;
                          for (const auto &result : published)
                          {
                              if (result.generation == generation2)
                              {
                                  ++generation2Count;
                              }
                          }
                          return generation2Count == 3;
                      }));

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    auto drained = worker.takePublished();
    published.insert(published.end(), std::make_move_iterator(drained.begin()),
                     std::make_move_iterator(drained.end()));

    int generation1Count = 0;
    int generation2Count = 0;
    for (const auto &result : published)
    {
        if (result.generation == generation1)
        {
            ++generation1Count;
        }
        if (result.generation == generation2)
        {
            ++generation2Count;
        }
    }

    REQUIRE(generation1Count > 0);
    REQUIRE(generation1Count < 100);
    REQUIRE(generation2Count == 3);
    REQUIRE(canceledChunksSeen.load() > 0);
}

TEST_CASE("LatestWinsBackgroundWorker collapses pending requests to the newest generation",
          "[concurrency]")
{
    using Worker =
        cupuacu::concurrency::LatestWinsBackgroundWorker<int, int>;

    std::vector<int> processedRequests;
    std::mutex processedMutex;

    Worker worker([&](const int &request, const std::uint64_t,
                      const Worker::CancelCheck &isCanceled,
                      const Worker::PublishFn &publish)
                  {
                      {
                          std::lock_guard lock(processedMutex);
                          processedRequests.push_back(request);
                      }
                      for (int chunk = 0; chunk < request; ++chunk)
                      {
                          if (isCanceled())
                          {
                              return;
                          }
                          publish(chunk);
                          std::this_thread::sleep_for(
                              std::chrono::milliseconds(2));
                      }
                  });

    worker.submit(100);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    worker.submit(7);
    const auto latestGeneration = worker.submit(3);

    std::vector<Worker::PublishedResult> published;
    REQUIRE(waitUntil([&]
                      {
                          auto drained = worker.takePublished();
                          published.insert(published.end(),
                                           std::make_move_iterator(drained.begin()),
                                           std::make_move_iterator(drained.end()));
                          int latestCount = 0;
                          for (const auto &result : published)
                          {
                              if (result.generation == latestGeneration)
                              {
                                  ++latestCount;
                              }
                          }
                          return latestCount == 3;
                      }));

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    {
        std::lock_guard lock(processedMutex);
        REQUIRE(processedRequests.size() >= 2);
        REQUIRE(processedRequests[0] == 100);
        REQUIRE(processedRequests.back() == 3);
    }

    for (const auto &result : published)
    {
        REQUIRE(result.generation != latestGeneration - 1);
    }
}
