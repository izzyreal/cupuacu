#include <catch2/catch_test_macros.hpp>
#include "TestPaths.hpp"
#include "storage/MemoryResources.hpp"
#include "concurrency/TaskScheduler.hpp"
#include <latch>

using namespace cupuacu;
using namespace cupuacu::storage;

TEST_CASE("Small peak pages and full sample blocks share byte capacity",
          "[audio-memory]")
{
    constexpr uint64_t budget = AudioBlockBytes + 32 * 4096;
    auto cache = std::make_shared<DecodedBlockCache>(budget);
    AudioBlockStore store(test::makeUniqueTestRoot("mixed-pages") / "store");
    std::vector<AudioBlock> blocks;
    std::vector<float> samples(AudioBlockFrames, .75f);
    blocks.push_back(store.append(samples));
    for (int i = 0; i < 32; ++i)
    {
        blocks.push_back(store.append(std::span(samples).first(1024)));
    }
    const auto read = [&](AudioBlock block)
    {
        std::array<float, 17> out;
        cache->read(store, block, block.frames - out.size(), out);
        CHECK(std::all_of(out.begin(), out.end(),
                          [](float v)
                          {
                              return v == .75f;
                          }));
    };
    for (auto block : blocks)
    {
        read(block);
    }
    const auto before = store.ioBytes();
    for (auto block : blocks)
    {
        read(block);
    }
    CHECK(store.ioBytes() == before);
    CHECK(cache->stats().residentBytes == budget);
    auto scratch = cache->reserveScratch(4096);
    CHECK(cache->stats().residentBytes + cache->stats().reservedBytes <=
          budget);
    for (auto block : blocks)
    {
        read(block);
    }
    CHECK(cache->stats().peakManagedBytes <= budget);
    cache->setPressure(2);
    CHECK(cache->stats().residentBytes + 4096 <= budget / 4);
    for (auto block : blocks)
    {
        read(block);
    }
    CHECK(cache->stats().inFlightBytes == 0);
}

TEST_CASE(
    "Concurrent mixed-size cache misses release their actual reservations",
    "[audio-memory]")
{
    constexpr uint64_t budget = AudioBlockBytes + 4096;
    auto cache = std::make_shared<DecodedBlockCache>(budget);
    const auto root = test::makeUniqueTestRoot("mixed-concurrent");
    const std::array<int, 4> sizes{65536, 1024, 5000, 33};
    std::vector<std::unique_ptr<AudioBlockStore>> stores;
    std::vector<AudioBlock> blocks;
    for (int i = 0; i < 4; ++i)
    {
        stores.push_back(
            std::make_unique<AudioBlockStore>(root / std::to_string(i)));
        blocks.push_back(
            stores.back()->append(std::vector<float>(sizes[i], float(i))));
    }
    std::latch start(4);
    std::vector<std::future<bool>> readers;
    for (int i = 0; i < 4; ++i)
    {
        readers.push_back(
            std::async(std::launch::async,
                       [&, i]
                       {
                           start.count_down();
                           start.wait();
                           for (int j = 0; j < 100; ++j)
                           {
                               std::array<float, 31> out;
                               cache->read(*stores[i], blocks[i],
                                           j % (sizes[i] - out.size()), out);
                               if (!std::all_of(out.begin(), out.end(),
                                                [i](float v)
                                                {
                                                    return v == i;
                                                }))
                               {
                                   return false;
                               }
                           }
                           return true;
                       }));
    }
    for (auto &reader : readers)
    {
        CHECK(reader.get());
    }
    CHECK(cache->stats().peakManagedBytes <= budget);
    std::array<float, 1> out;
    CHECK_THROWS(cache->read(*stores[0], {UINT64_MAX, 0, 1024}, 0, out));
    CHECK(cache->stats().inFlightBytes == 0);
    auto scratch = cache->reserveScratch(budget);
    CHECK(cache->stats().residentBytes == 0);
}

TEST_CASE("Shared sample capacity yields to scratch and pressure",
          "[audio-memory]")
{
    auto cache = std::make_shared<DecodedBlockCache>(8 * AudioBlockBytes);
    AudioBlockStore store(test::makeUniqueTestRoot("memory") / "store");
    std::vector<float> samples(AudioBlockFrames, .25f);
    std::vector<AudioBlock> blocks;
    for (int i = 0; i < 12; ++i)
    {
        blocks.push_back(store.append(samples));
    }
    auto readAll = [&]
    {
        for (auto block : blocks)
        {
            std::array<float, 7> output{};
            cache->read(store, block, 17, output);
            CHECK(output == std::array<float, 7>{.25f, .25f, .25f, .25f, .25f,
                                                 .25f, .25f});
        }
    };
    readAll();
    CHECK(cache->stats().residentBytes == 8 * AudioBlockBytes);
    auto scratch = cache->reserveScratch(3 * AudioBlockBytes);
    CHECK(cache->stats().residentBytes == 5 * AudioBlockBytes);
    readAll();
    CHECK(cache->stats().peakManagedBytes == 8 * AudioBlockBytes);
    cache->setPressure(1);
    CHECK(cache->stats().residentBytes == AudioBlockBytes);
    cache->setPressure(2);
    CHECK(cache->stats().residentBytes == 0);
    CHECK(cache->stats().reservedBytes == 3 * AudioBlockBytes);
    readAll(); // No spare cache capacity: direct reads still succeed.
    scratch.reset();
    readAll();
    CHECK(cache->stats().residentBytes == 2 * AudioBlockBytes);
    cache->setPressure(0);
    CHECK(cache->stats().residentBytes == 2 * AudioBlockBytes);
    cache->setByteBudget(0);
    CHECK(cache->stats().residentBytes == 0);
    CHECK_THROWS(cache->reserveScratch(1));
}

TEST_CASE("Concurrent stores respect one budget and failed reads release slots",
          "[audio-memory]")
{
    auto cache = std::make_shared<DecodedBlockCache>(2 * AudioBlockBytes);
    const auto root = test::makeUniqueTestRoot("memory-concurrent");
    std::vector<std::unique_ptr<AudioBlockStore>> stores;
    std::vector<AudioBlock> blocks;
    for (int i = 0; i < 4; ++i)
    {
        stores.push_back(
            std::make_unique<AudioBlockStore>(root / std::to_string(i)));
        std::vector<float> samples(AudioBlockFrames, float(i));
        blocks.push_back(stores.back()->append(samples));
    }
    std::latch start(4);
    std::vector<std::future<bool>> readers;
    for (int i = 0; i < 4; ++i)
    {
        readers.push_back(
            std::async(std::launch::async,
                       [&, i]
                       {
                           start.count_down();
                           start.wait();
                           for (int j = 0; j < 200; ++j)
                           {
                               std::array<float, 31> output{};
                               cache->read(*stores[i], blocks[i], j, output);
                               if (!std::all_of(output.begin(), output.end(),
                                                [i](float v)
                                                {
                                                    return v == i;
                                                }))
                               {
                                   return false;
                               }
                           }
                           return true;
                       }));
    }
    for (auto &reader : readers)
    {
        CHECK(reader.get());
    }
    CHECK(cache->stats().peakManagedBytes <= 2 * AudioBlockBytes);
    std::array<float, 1> output{};
    CHECK_THROWS(cache->read(*stores[0], {UINT64_MAX, 0, 1}, 0, output));
    CHECK(cache->stats().inFlightBytes == 0);
    auto scratch = cache->reserveScratch(2 * AudioBlockBytes);
    CHECK(cache->stats().residentBytes == 0);
}

TEST_CASE(
    "Independent schedulers share scratch admission and release failed jobs",
    "[audio-memory]")
{
    auto cache = std::make_shared<DecodedBlockCache>(100);
    concurrency::TaskScheduler first(1, 8, 100, cache),
        second(1, 8, 100, cache);
    std::promise<void> release, began;
    const auto ready = release.get_future().share();
    auto job = first.submit(
        [&]
        {
            began.set_value();
            ready.wait();
        },
        {.scratchBytes = 75});
    began.get_future().wait();
    auto other = second.submit(
        []
        {
            throw std::runtime_error("expected");
        },
        {.scratchBytes = 50});
    CHECK(cache->stats().reservedBytes == 75);
    release.set_value();
    job.get();
    CHECK_THROWS(other.get());
    CHECK(cache->stats().reservedBytes == 0);
    CHECK(cache->stats().peakManagedBytes <= 100);
    CHECK_THROWS(second.submit([] {}, {.scratchBytes = 101}));
}

TEST_CASE("Audio memory configuration validates integer limits",
          "[audio-memory]")
{
    const auto path =
        test::makeUniqueTestRoot("memory-settings") / "performance.json";
    CHECK(readAudioMemoryBudget(path, 10000) == 1000);
    std::filesystem::create_directories(path.parent_path());
    for (const auto text : {"{}", "{\"audio_memory_mib\":0}"})
    {
        {
            std::ofstream out(path);
            out << text;
        }
        CHECK(readAudioMemoryBudget(path, 10000) == 1000);
    }
    {
        std::ofstream out(path);
        out << "{\"audio_memory_mib\":64}";
    }
    CHECK(readAudioMemoryBudget(path, 10000) == 64 * 1024 * 1024);
    for (const auto text : {"[]", "bad", "{\"audio_memory_mib\":-1}",
                            "{\"audio_memory_mib\":0.5}",
                            "{\"audio_memory_mib\":18446744073709551615}"})
    {
        {
            std::ofstream out(path);
            out << text;
        }
        CHECK_THROWS(readAudioMemoryBudget(path, 10000));
    }
    CHECK(defaultDecodedBlockCache() == defaultDecodedBlockCache());
    CHECK(State{}.importSampleCache == defaultDecodedBlockCache());
}

TEST_CASE("Memory pressure requests reach the background trimmer",
          "[audio-memory]")
{
    auto cache = std::make_shared<DecodedBlockCache>(8 * AudioBlockBytes);
    MemoryPressureMonitor monitor(cache);
    monitor.notify(2);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (cache->stats().targetBytes != 2 * AudioBlockBytes &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::yield();
    }
    CHECK(cache->stats().targetBytes == 2 * AudioBlockBytes);
}
