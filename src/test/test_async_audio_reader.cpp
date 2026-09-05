#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include "storage/AsyncAudioReader.hpp"
#include <chrono>
#include <future>
#include <atomic>

using namespace cupuacu::storage;
using namespace std::chrono_literals;

namespace
{
    struct Gate
    {
        std::promise<void> entered, release;
        std::shared_future<void> released = release.get_future().share();
        std::promise<std::thread::id> destroyed;
        std::atomic<int> reads{0};
    };

    class GatedReader final : public AudioReader
    {
        std::shared_ptr<Gate> gate;

    public:
        explicit GatedReader(std::shared_ptr<Gate> value)
            : gate(std::move(value))
        {
        }
        ~GatedReader() override
        {
            gate->destroyed.set_value(std::this_thread::get_id());
        }
        AudioShape shape() const override
        {
            return {1000000, 1, 48000};
        }
        void readChannel(int channel, int64_t start,
                         std::span<float> output) const override
        {
            validateRange(shape(), channel, start, output.size());
            if (gate->reads.fetch_add(1) == 0)
            {
                gate->entered.set_value();
                // Timeout keeps a broken implementation from hanging the suite.
                if (gate->released.wait_for(2s) != std::future_status::ready)
                {
                    throw std::runtime_error("Test read was not released");
                }
            }
            if (start == 999999)
            {
                throw std::runtime_error("Injected read failure");
            }
            for (std::size_t i = 0; i < output.size(); ++i)
            {
                output[i] = float(start + i);
            }
        }
    };

    std::optional<AsyncAudioReader::Result>
    awaitResult(AsyncAudioReader &reader)
    {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (auto result = reader.takePublished())
            {
                return result;
            }
            std::this_thread::sleep_for(1ms);
        }
        return {};
    }
} // namespace

TEST_CASE(
    "Async audio reads supersede blocked windows and recover after I/O errors",
    "[async-audio]")
{
    auto gate = std::make_shared<Gate>();
    auto entered = gate->entered.get_future();
    AsyncAudioReader reader(std::make_shared<GatedReader>(gate), 131072);
    reader.submit(0, 0, 131072);
    REQUIRE(entered.wait_for(2s) == std::future_status::ready);
    // The worker is blocked inside readChannel; none of these calls may wait.
    REQUIRE_FALSE(reader.takePublished());
    for (int i = 1; i < 100; ++i)
    {
        reader.submit(0, i, 17);
    }
    const auto latest = reader.submit(0, 65530, 19);
    REQUIRE_THROWS_AS(reader.submit(0, 0, 131073), std::length_error);
    REQUIRE_THROWS_AS(reader.submit(1, 0, 17), std::out_of_range);
    REQUIRE_THROWS_AS(reader.submit(0, INT64_MAX, 17), std::out_of_range);
    REQUIRE(gate->reads == 1);
    gate->release.set_value();
    auto result = awaitResult(reader);
    REQUIRE(result);
    REQUIRE(result->generation == latest);
    REQUIRE_FALSE(result->error);
    REQUIRE(result->samples.size() == 19);
    for (int i = 0; i < 19; ++i)
    {
        REQUIRE(result->samples[i] == float(65530 + i));
    }
    REQUIRE(gate->reads == 2); // Old request stopped before its second block.
    REQUIRE_FALSE(reader.takePublished());

    const auto failure = reader.submit(0, 999999, 1);
    result = awaitResult(reader);
    REQUIRE(result);
    REQUIRE(result->generation == failure);
    REQUIRE(result->error);
    REQUIRE_THROWS_WITH(std::rethrow_exception(result->error),
                        "Injected read failure");
    const auto recovered = reader.submit(0, 999998, 1);
    result = awaitResult(reader);
    REQUIRE(result);
    REQUIRE(result->generation == recovered);
    REQUIRE_FALSE(result->error);
    REQUIRE(result->samples[0] == 999998);
    reader.close();
    REQUIRE_THROWS_AS(reader.submit(0, 0, 1), std::logic_error);
    reader.waitUntilClosed();
}

TEST_CASE(
    "Closing async audio does not wait for a blocked read and releases "
    "ownership on the worker",
    "[async-audio]")
{
    auto gate = std::make_shared<Gate>();
    auto entered = gate->entered.get_future();
    auto destroyed = gate->destroyed.get_future();
    auto reader = std::make_unique<AsyncAudioReader>(
        std::make_shared<GatedReader>(gate), 131072);
    reader->submit(0, 0, 131072);
    REQUIRE(entered.wait_for(2s) == std::future_status::ready);
    reader.reset();
    // Last reader reference must still be in the blocked worker after close.
    REQUIRE(destroyed.wait_for(0ms) == std::future_status::timeout);
    gate->release.set_value();
    REQUIRE(destroyed.wait_for(2s) == std::future_status::ready);
    REQUIRE(destroyed.get() != std::this_thread::get_id());
    REQUIRE(gate->reads == 1);
}
