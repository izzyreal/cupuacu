#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "TestPaths.hpp"
#include "storage/MemoryResources.hpp"
#include "storage/WorkingAllocator.hpp"
#include "concurrency/TaskScheduler.hpp"
#include "waveform/WaveformViewport.hpp"
#include <latch>

using namespace cupuacu;
using namespace cupuacu::storage;

TEST_CASE(
    "Working reservations share cache capacity and fail without nested waits",
    "[working-memory]")
{
    auto memory = std::make_shared<DecodedBlockCache>(1024);
    auto index = memory->tryReserveWorking(512, MemoryUse::Index);
    auto peaks = memory->tryReserveWorking(256, MemoryUse::Peaks);
    REQUIRE(index);
    REQUIRE(peaks);
    CHECK(memory->stats().workingBytes == 768);
    CHECK(memory->stats().workingByUse[unsigned(MemoryUse::Index)] == 512);
    CHECK_FALSE(memory->tryReserveWorking(257, MemoryUse::Viewport));
    CHECK_THROWS(memory->reserveScratch(257));
    auto scratch = memory->reserveScratch(256);
    CHECK(memory->stats().peakManagedBytes == 1024);
    memory->setPressure(2);
    CHECK_FALSE(memory->tryReserveWorking(1, MemoryUse::Transport));
    CHECK(memory->stats().workingBytes == 768);
    scratch.reset();
    peaks.reset();
    index.reset();
    CHECK(memory->stats().reservedBytes == 0);
    auto allowed = reserveWorking(256, MemoryUse::Transport, memory);
    CHECK(allowed);
    CHECK_THROWS(reserveWorking(1, MemoryUse::Import, memory));
    allowed.reset();
    CHECK(memory->stats().workingBytes == 0);
}

TEST_CASE("Index buffers share admission and use disk when no buffer fits",
          "[working-memory]")
{
    const uint64_t budget = GENERATE(0, 4096, 8192);
    auto memory = std::make_shared<DecodedBlockCache>(budget);
    {
        RecordIndex<uint64_t> first(memory), second(memory);
        for (uint64_t i = 0; i < 2000; ++i)
        {
            first.push_back(i * 3);
            second.push_back(i * 7);
        }
        first.setBack(19);
        first.set(255, 17);
        first.seal();
        second.seal();
        for (uint64_t i = 0; i < 2000; ++i)
        {
            CHECK(first[i] == (i == 1999 ? 19 : i == 255 ? 17 : i * 3));
            CHECK(second[i] == i * 7);
        }
        CHECK(first.back() == 19);
        CHECK(memory->stats().peakManagedBytes <= budget);
        CHECK(first.stats().residentBytes + second.stats().residentBytes ==
              memory->stats().workingBytes);
    }
    CHECK(memory->stats().reservedBytes == 0);
}

TEST_CASE("Concurrent working allocations never exceed shared admission",
          "[working-memory]")
{
    auto memory = std::make_shared<DecodedBlockCache>(4096);
    std::latch start(8);
    std::vector<std::future<void>> jobs;
    for (unsigned i = 0; i < 8; ++i)
    {
        jobs.push_back(std::async(
            std::launch::async,
            [&, i]
            {
                start.count_down();
                start.wait();
                for (int n = 0; n < 1000; ++n)
                {
                    auto token = memory->tryReserveWorking(
                        1024, MemoryUse(i % unsigned(MemoryUse::Count)));
                    if (token)
                    {
                        std::vector<uint8_t> bytes(1024);
                        std::this_thread::yield();
                    }
                }
            }));
    }
    for (auto &job : jobs)
    {
        job.get();
    }
    CHECK(memory->stats().peakManagedBytes <= 4096);
    CHECK(memory->stats().reservedBytes == 0);
    CHECK_FALSE(memory->tryReserveWorking(UINT64_MAX, MemoryUse::Index));
}

TEST_CASE("Admission failure completes queued jobs and reports the cause",
          "[working-memory]")
{
    auto memory = std::make_shared<DecodedBlockCache>(1024);
    auto retained = reserveWorking(900, MemoryUse::Peaks, memory);
    concurrency::TaskScheduler scheduler(1, 8, 1024, memory);
    std::atomic<bool> ran = false, reported = false;
    auto ticket = scheduler.submit(
        [&]
        {
            ran = true;
        },
        {.scratchBytes = 256,
         .admissionFailed = [&](std::exception_ptr error)
         {
             reported = bool(error);
         }});
    REQUIRE(ticket.completion.wait_for(std::chrono::seconds(2)) ==
            std::future_status::ready);
    CHECK_THROWS(ticket.get());
    CHECK(reported.load());
    CHECK_FALSE(ran.load());
    CHECK(memory->stats().reservedBytes == 900);
}

TEST_CASE(
    "Viewport publication retains its memory until the result is released",
    "[working-memory]")
{
    class Reader final : public AudioReader
    {
    public:
        AudioShape shape() const override
        {
            return {1024, 1, 48000, SampleFormat::FLOAT32};
        }
        void readChannel(int channel, int64_t first,
                         std::span<float> out) const override
        {
            validateRange(shape(), channel, first, out.size());
            std::fill(out.begin(), out.end(), .25f);
        }
    };
    auto memory = std::make_shared<DecodedBlockCache>(4096);
    waveform::ViewportSource source;
    source.audio = std::make_shared<Reader>();
    source.memory = memory;
    auto first = waveform::WaveformViewport::compute(source, {0, 0, .5, 128},
                                                     []
                                                     {
                                                         return false;
                                                     });
    REQUIRE(first);
    CHECK(memory->stats().workingBytes ==
          first->samples.capacity() * sizeof(float) + first->dirty.capacity());
    auto second = waveform::WaveformViewport::compute(source, {0, 64, .5, 128},
                                                      []
                                                      {
                                                          return false;
                                                      });
    REQUIRE(second);
    const auto expected =
        second->samples.capacity() * sizeof(float) + second->dirty.capacity();
    first = std::move(second);
    CHECK(memory->stats().workingBytes == expected);
    first.reset();
    second.reset();
    CHECK(memory->stats().workingBytes == 0);
    memory->setByteBudget(1);
    CHECK_THROWS(waveform::WaveformViewport::compute(source, {0, 0, .5, 128},
                                                     []
                                                     {
                                                         return false;
                                                     }));
    CHECK(memory->stats().reservedBytes == 0);
}

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

TEST_CASE("Transport headroom survives bulk admission and pressure",
          "[working-memory]")
{
    auto memory = std::make_shared<DecodedBlockCache>(8192, true);
    auto bulk = reserveWorking(6144, MemoryUse::Container, memory);
    CHECK_FALSE(memory->tryReserveWorking(1, MemoryUse::Import));
    auto transport = reserveWorking(2048, MemoryUse::Transport, memory);
    CHECK(memory->stats().peakManagedBytes == 8192);
    CHECK_FALSE(memory->tryReserveWorking(1, MemoryUse::Transport));
    bulk.reset();
    memory->setPressure(2);
    CHECK_FALSE(memory->tryReserveWorking(1, MemoryUse::Container));
    transport.reset();
    auto allowed = reserveWorking(1536, MemoryUse::Container, memory);
    CHECK_FALSE(memory->tryReserveWorking(1, MemoryUse::Export));
    auto remaining = reserveWorking(512, MemoryUse::Transport, memory);
    CHECK(memory->stats().reservedBytes == 2048);
}

TEST_CASE("Container allocations retain admission through growth copy and move",
          "[working-memory]")
{
    using Allocator = WorkingAllocator<uint64_t, MemoryUse::Container>;
    using Vector = WorkingVector<uint64_t, MemoryUse::Container>;
    auto memory = std::make_shared<DecodedBlockCache>(8192);
    {
        Vector first{Allocator(memory)};
        first.resize(256, 42);
        const auto one = memory->stats().workingBytes;
        CHECK(one >= first.capacity() * sizeof(uint64_t));
        auto second = first;
        CHECK(memory->stats().workingBytes == 2 * one);
        Vector moved(std::move(second));
        CHECK(memory->stats().workingBytes == 2 * one);
        CHECK_THROWS(first.reserve(8192));
        CHECK(first.size() == 256);
        CHECK(first.front() == 42);
        CHECK(memory->stats().workingBytes == 2 * one);
    }
    CHECK(memory->stats().workingBytes == 0);
    struct alignas(64) Aligned
    {
        int value;
    };
    {
        WorkingVector<Aligned, MemoryUse::Container> values{
            WorkingAllocator<Aligned, MemoryUse::Container>(memory)};
        values.resize(4);
        CHECK(reinterpret_cast<uintptr_t>(values.data()) % 64 == 0);
    }
    CHECK(memory->stats().workingBytes == 0);
}
