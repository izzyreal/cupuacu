#include <catch2/catch_test_macros.hpp>
#include "concurrency/TaskScheduler.hpp"
#include <latch>

using cupuacu::concurrency::TaskScheduler;
namespace
{
    // Release blocked workers even when an assertion throws.
    struct Gate
    {
        std::promise<void> promise;
        std::shared_future<void> ready = promise.get_future().share();
        bool released = false;
        void release()
        {
            if (!released)
            {
                released = true;
                promise.set_value();
            }
        }
        ~Gate()
        {
            release();
        }
    };
} // namespace
TEST_CASE("Bulk scheduling reserves memory and runs two jobs concurrently",
          "[scheduler]")
{
    TaskScheduler scheduler(2, 8, 100);
    Gate gate;
    std::latch started(2);
    auto work = [&]
    {
        started.count_down();
        gate.ready.wait();
    };
    auto first = scheduler.submit(work, {.scratchBytes = 60});
    auto second = scheduler.submit(work, {.scratchBytes = 40});
    started.wait();
    auto queued = scheduler.submit([] {}, {.scratchBytes = 1});
    const auto stats = scheduler.stats();
    CHECK(stats.running == 2);
    CHECK(stats.queued == 1);
    CHECK(stats.reservedBytes == 100);
    CHECK_THROWS(scheduler.submit([] {}, {.scratchBytes = 101}));
    gate.release();
    queued.get();
    CHECK(scheduler.stats().peakReservedBytes == 100);
}
TEST_CASE("Autosave deadlines precede user work and maintenance follows it",
          "[scheduler]")
{
    TaskScheduler scheduler(1, 8);
    Gate gate;
    std::latch started(1);
    auto blocker = scheduler.submit(
        [&]
        {
            started.count_down();
            gate.ready.wait();
        },
        {});
    started.wait();
    std::vector<int> order;
    auto maintenance = scheduler.submit(
        [&]
        {
            order.push_back(3);
        },
        {.priority = TaskScheduler::Priority::Maintenance});
    auto user = scheduler.submit(
        [&]
        {
            order.push_back(2);
        },
        {});
    auto autosave = scheduler.submit(
        [&]
        {
            order.push_back(1);
        },
        {.priority = TaskScheduler::Priority::Autosave,
         .deadline = std::chrono::steady_clock::now()});
    gate.release();
    maintenance.get();
    CHECK(order == std::vector<int>{1, 2, 3});
}
TEST_CASE("Completed unpublished jobs retain bounded admission slots",
          "[scheduler]")
{
    TaskScheduler scheduler(1, 1);
    auto first = scheduler.submit([] {}, {});
    first.get();
    auto second = scheduler.submit([] {}, {});
    second.get();
    CHECK_THROWS(scheduler.submit([] {}, {}));
    first = {};
    auto throwing = scheduler.submit(
        []
        {
            throw std::runtime_error("failure");
        },
        {});
    CHECK_THROWS_AS(throwing.get(), std::runtime_error);
    second = {};
    auto survivor = scheduler.submit([] {}, {});
    CHECK_NOTHROW(survivor.get());
}
