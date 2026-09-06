#include "waveform/StreamingPeakBuilder.hpp"
#include <catch2/catch_test_macros.hpp>
#include "DocumentSession.hpp"
#include "LongTask.hpp"
#include "concurrency/DeferredRelease.hpp"
#include "TestPaths.hpp"
#include "storage/AudioEditRevision.hpp"
#include "waveform/WaveformViewport.hpp"
#include <chrono>
#include <future>
#include <atomic>

using namespace cupuacu;
using namespace std::chrono_literals;
namespace
{
    std::optional<waveform::WaveformViewport::Result>
    awaitViewport(waveform::WaveformViewport &worker)
    {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (auto result = worker.takePublished())
            {
                return result;
            }
            std::this_thread::sleep_for(1ms);
        }
        return {};
    }
    void initialize(DocumentSession &session, int64_t frames)
    {
        session.document.initialize(SampleFormat::FLOAT32, 48000, 2, frames);
        std::vector<float> samples(frames * 2);
        for (int64_t i = 0; i < frames; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                samples[i * 2 + c] = float(i % 997 - 498 + c) / 512;
            }
        }
        session.document.writeInterleavedFloatBlock(0, samples.data(), frames,
                                                    2, false);
        session.rebuildWaveformCacheSynchronously();
    }
} // namespace

TEST_CASE(
    "Session viewport snapshots survive edits and invalidate on document "
    "replacement",
    "[viewport-pipeline]")
{
    DocumentSession session;
    initialize(session, 8193);
    auto first = session.getViewportSource();
    REQUIRE(first);
    REQUIRE(session.getViewportSource() == first);
    session.document.addMarker(20,
                               "Marker-only changes reuse the audio snapshot");
    REQUIRE(session.getViewportSource() == first);
    waveform::WaveformViewport worker(*first);
    const auto generation = worker.submit({1, 17, 1, 1024});
    session.document.setSample(1, 17, 99);
    session.invalidateWaveformSamples(17, 17);
    auto result = awaitViewport(worker);
    REQUIRE(result);
    REQUIRE(result->generation == generation);
    REQUIRE_FALSE(result->error);
    REQUIRE(result->value);
    REQUIRE(result->value->peaks[0].max == float(17 - 498 + 1) / 512);
    session.rebuildWaveformCacheSynchronously();
    auto second = session.getViewportSource();
    REQUIRE(second != first);
    waveform::WaveformViewport changed(*second);
    changed.submit({1, 17, 0.25, 1024});
    result = awaitViewport(changed);
    REQUIRE(result);
    REQUIRE(result->value);
    REQUIRE(result->value->sampleAt(17) == 99);
    // A different document can have the same numeric version.
    DocumentSession other;
    initialize(other, 8193);
    session.document = other.document;
    session.waveformCaches = other.waveformCaches;
    REQUIRE(session.getViewportSource() != second);
    worker.close();
    worker.waitUntilClosed();
    changed.close();
    changed.waitUntilClosed();
}

TEST_CASE("Disk revisions use the same session viewport pipeline at every zoom",
          "[viewport-pipeline]")
{
    DocumentSession reference;
    initialize(reference, 131089);
    const auto shape = reference.getViewportSource()->audio->shape();
    auto store = std::make_shared<storage::AudioBlockStore>(
        test::makeUniqueTestRoot("session-viewport") / "store");
    storage::AudioRevisionBuilder builder(
        shape, store,
        std::make_shared<storage::DecodedBlockCache>(storage::AudioBlockBytes));
    std::vector<float> input(shape.frames * 2);
    for (int64_t i = 0; i < shape.frames; ++i)
    {
        for (int c = 0; c < 2; ++c)
        {
            input[i * 2 + c] = reference.document.getSample(c, i);
        }
    }
    builder.appendInterleaved(input);
    std::vector<std::vector<gui::PeakLevel>> levels;
    for (int c = 0; c < 2; ++c)
    {
        levels.push_back(
            reference.getWaveformCache(c).snapshotBuildState().levels);
    }
    auto revision = storage::AudioEditRevision::from(builder.finish(
        {}, std::make_shared<waveform::SourcePeaks>(shape, std::move(levels))));
    DocumentSession session;
    session.document.initialize(shape.format, shape.sampleRate, shape.channels,
                                shape.frames); // Metadata only.
    session.bindReadRevision(revision);
    REQUIRE(session.hasReadRevision());
    REQUIRE_FALSE(session.getWaveformCacheBuildProgress());
    auto source = session.getViewportSource();
    waveform::WaveformViewport worker(*source);
    for (const double spp : {0.125, 1.0, 7.3, 127.9, 128.0, 512.7})
    {
        worker.submit({0, 17, spp, 257});
        auto result = awaitViewport(worker);
        REQUIRE(result);
        REQUIRE_FALSE(result->error);
        REQUIRE(result->value);
        const auto &data = *result->value;
        REQUIRE_FALSE(data.pending);
        if (spp < 1)
        {
            for (std::size_t i = 0; i < data.samples.size(); ++i)
            {
                REQUIRE(data.samples[i] == input[(data.rawStart + i) * 2]);
            }
        }
        else
        {
            for (int x = 0; x < 257; ++x)
            {
                const auto start = std::min<int64_t>(
                    shape.frames, int64_t(std::floor(17 + x * spp)));
                const auto end = std::min<int64_t>(
                    shape.frames, int64_t(std::floor(17 + (x + 1) * spp)));
                auto expected = waveform::emptyPeak();
                for (auto i = start; i < end; ++i)
                {
                    expected = waveform::combine(expected,
                                                 {input[i * 2], input[i * 2]});
                }
                REQUIRE(data.peaks[x].min <= expected.min);
                REQUIRE(data.peaks[x].max >= expected.max);
                if (spp < 128)
                {
                    REQUIRE(data.peaks[x].min == expected.min);
                    REQUIRE(data.peaks[x].max == expected.max);
                }
            }
        }
    }
    const auto before = store->ioBytes();
    worker.submit({0, 17, 1024, 257});
    REQUIRE(awaitViewport(worker));
    REQUIRE(store->ioBytes() == before); // Overview never loads sample blocks.
    REQUIRE_THROWS_AS(
        worker.submit({0, 0, 1, waveform::WaveformViewport::maxWidth + 1}),
        std::invalid_argument);
    REQUIRE_THROWS_AS(
        worker.submit({0, 0, std::numeric_limits<double>::infinity(), 100}),
        std::invalid_argument);
    REQUIRE_THROWS_AS(session.document.setSample(0, 0, 1), std::logic_error);
    REQUIRE(session.getViewportSource());
    worker.close();
    worker.waitUntilClosed();
}

TEST_CASE(
    "Closing a viewport while its reader is blocked never joins the worker",
    "[viewport-pipeline]")
{
    struct Gate
    {
        std::promise<void> entered, release;
        std::shared_future<void> released = release.get_future().share();
        std::promise<std::thread::id> destroyed;
    };
    struct Reader final : storage::AudioReader
    {
        std::shared_ptr<Gate> gate;
        explicit Reader(std::shared_ptr<Gate> value) : gate(std::move(value)) {}
        ~Reader() override
        {
            gate->destroyed.set_value(std::this_thread::get_id());
        }
        storage::AudioShape shape() const override
        {
            return {1000000, 1, 48000};
        }
        void readChannel(int, int64_t, std::span<float> output) const override
        {
            gate->entered.set_value();
            if (gate->released.wait_for(2s) != std::future_status::ready)
            {
                throw std::runtime_error("Read timed out");
            }
            std::fill(output.begin(), output.end(), 0.25f);
        }
    };
    auto gate = std::make_shared<Gate>();
    auto entered = gate->entered.get_future();
    auto destroyed = gate->destroyed.get_future();
    auto worker = std::make_unique<waveform::WaveformViewport>(
        waveform::ViewportSource{std::make_shared<Reader>(gate), {}, {}});
    worker->submit({0, 0, 1, 1024});
    REQUIRE(entered.wait_for(2s) == std::future_status::ready);
    for (int i = 0; i < 100; ++i)
    {
        worker->submit({0, i, 1, 1024});
    }
    worker.reset();
    REQUIRE(destroyed.wait_for(0ms) == std::future_status::timeout);
    gate->release.set_value();
    REQUIRE(destroyed.wait_for(2s) == std::future_status::ready);
    REQUIRE(destroyed.get() != std::this_thread::get_id());
}

TEST_CASE("Session ownership releases unviewed revisions on the reclaimer",
          "[viewport-pipeline]")
{
    struct Probe
    {
        std::promise<std::thread::id> &done;
        ~Probe()
        {
            done.set_value(std::this_thread::get_id());
        }
    };
    std::promise<std::thread::id> done;
    auto finished = done.get_future();
    auto value = std::shared_ptr<Probe>(new Probe{done});
    auto retained = concurrency::releaseOnWorker(std::move(value));
    retained.reset();
    REQUIRE(finished.wait_for(2s) == std::future_status::ready);
    REQUIRE(finished.get() != std::this_thread::get_id());
}

TEST_CASE(
    "Paged source peaks match resident queries across scalar block boundaries",
    "[paged-peaks]")
{
    constexpr int64_t count = 9 * 65536 + 17;
    const storage::AudioShape shape{count * 128 - 13, 2, 48000,
                                    SampleFormat::FLOAT32};
    std::vector<std::vector<gui::PeakLevel>> levels(2);
    for (int c = 0; c < 2; ++c)
    {
        levels[c].resize(1);
        levels[c][0].resize(count);
        for (int64_t i = 0; i < count; ++i)
        {
            levels[c][0].set(i, {-float(i % 103 + c), float(i % 107 + c)});
        }
    }
    waveform::SourcePeaks resident(shape, levels);
    auto cache =
        std::make_shared<storage::DecodedBlockCache>(storage::AudioBlockBytes);
    auto paged =
        waveform::SourcePeaks::createPaged(shape, std::move(levels), cache);
    CHECK(paged->residency().pagedBytes > 8 * 1024 * 1024);
    CHECK(paged->residency().residentBytes < 128 * 1024);
    for (int c = 0; c < 2; ++c)
    {
        for (int64_t first : {0, 32767, 65535, 131071, int(count - 97)})
        {
            const auto end = std::min(count, first + 34567);
            uint64_t a = 0, b = 0;
            const auto expected = resident.queryBlocks(c, first, end, a);
            const auto actual = paged->queryBlocks(c, first, end, b);
            CHECK(actual.min == expected.min);
            CHECK(actual.max == expected.max);
            CHECK(a == b);
        }
        std::vector<waveform::Peak> actual(34567), expected(actual.size());
        for (int l = 0; l < 4; ++l)
        {
            resident.readPeaks(c, l, 32761, expected);
            paged->readPeaks(c, l, 32761, actual);
            for (std::size_t i = 0; i < actual.size(); ++i)
            {
                REQUIRE(actual[i].min == expected[i].min);
                REQUIRE(actual[i].max == expected[i].max);
            }
        }
    }
    CHECK(cache->stats().peakResidentBytes <= storage::AudioBlockBytes);
    CHECK(paged->residency().bytesRead > 0);
    cache->setByteBudget(0);
    uint64_t a = 0, b = 0;
    CHECK(paged->queryBlocks(1, 1, 9001, a).max ==
          resident.queryBlocks(1, 1, 9001, b).max);
    CHECK(cache->stats().residentBytes == 0);
    CHECK_THROWS(paged->queryBlocks(0, 0, count + 1, a));
}

TEST_CASE("Small peak pyramids remain resident and paging checks cancellation",
          "[paged-peaks]")
{
    storage::AudioShape shape{4096 * 128, 1, 48000, SampleFormat::FLOAT32};
    gui::PeakLevel level;
    level.resize(4096);
    auto small = waveform::SourcePeaks::createPaged(shape, {{level}});
    CHECK(small->residency().pagedBytes == 0);
    shape.frames *= 16;
    level.resize(65536);
    int checks = 0;
    CHECK_THROWS_AS(waveform::SourcePeaks::createPaged(shape, {{level}}, {},
                                                       [&]
                                                       {
                                                           return ++checks == 3;
                                                       }),
                    LongTaskCanceledError);
    CHECK(checks == 3);
}

TEST_CASE("Streaming pyramid matches resident levels without full input reads",
          "[streaming-peaks]")
{
    constexpr uint64_t count = 9 * 65536 + 17;
    storage::AudioShape shape{int64_t(count * 128 - 13), 2, 48000,
                              SampleFormat::FLOAT32};
    std::vector<std::vector<gui::PeakLevel>> levels(2);
    auto value = [](int c, uint64_t i)
    {
        return waveform::Peak{-float(i % 103 + c), float(i % 107 + c)};
    };
    for (int c = 0; c < 2; ++c)
    {
        levels[c].resize(1);
        levels[c][0].resize(count);
        for (uint64_t i = 0; i < count; ++i)
        {
            levels[c][0].set(i, value(c, i));
        }
    }
    waveform::SourcePeaks reference(shape, std::move(levels));
    uint64_t read = 0, largest = 0;
    auto cache =
        std::make_shared<storage::DecodedBlockCache>(storage::AudioBlockBytes);
    auto streamed = waveform::SourcePeaks::createStreaming(
        shape,
        [&](int c, uint64_t first, std::span<waveform::Peak> out)
        {
            read += out.size();
            largest = std::max<uint64_t>(largest, out.size());
            for (std::size_t i = 0; i < out.size(); ++i)
            {
                out[i] = value(c, first + i);
            }
        },
        cache);
    CHECK(read == count * 2);
    CHECK(largest <= 16384);
    for (int c = 0; c < 2; ++c)
    {
        for (std::size_t l = 0, n = count;; ++l, n = (n + 1) / 2)
        {
            REQUIRE(streamed->levelSize(c, l) == n);
            const auto take = std::min<std::size_t>(n, 8193);
            std::vector<waveform::Peak> actual(take), expected(take);
            for (auto first : {std::size_t(0), n - take})
            {
                streamed->readPeaks(c, l, first, actual);
                reference.readPeaks(c, l, first, expected);
                for (std::size_t i = 0; i < take; ++i)
                {
                    REQUIRE(actual[i].min == expected[i].min);
                    REQUIRE(actual[i].max == expected[i].max);
                }
            }
            if (n <= 1)
            {
                break;
            }
        }
    }
    CHECK(streamed->residency().residentBytes < 128 * 1024);
    CHECK(cache->stats().peakResidentBytes <= storage::AudioBlockBytes);
}

TEST_CASE(
    "Streaming samples preserve bucket boundaries across arbitrary appends",
    "[streaming-peaks]")
{
    for (const int64_t frames : {int64_t(197), int64_t(128 * 32771 - 13)})
    {
        storage::AudioShape shape{frames, 2, 48000, SampleFormat::FLOAT32};
        waveform::StreamingPeakBuilder builder(shape);
        auto sample = [](int c, int64_t i)
        {
            return float((i % 137) - 68 + c) / 128;
        };
        auto reader = [&](int c, int64_t first, std::span<float> out)
        {
            for (std::size_t i = 0; i < out.size(); ++i)
            {
                out[i] = sample(c, first + i);
            }
        };
        for (int64_t first = 0; first < frames;)
        {
            first += std::min<int64_t>(first % 2 ? 65539 : 127, frames - first);
            builder.appendFrom(shape, first, reader);
        }
        auto peaks = builder.finish();
        CHECK_THROWS(builder.finish());
        for (int c = 0; c < 2; ++c)
        {
            for (int64_t block : {int64_t(0), int64_t(1), (frames - 1) / 128})
            {
                auto expected = waveform::emptyPeak();
                for (int64_t i = block * 128;
                     i < std::min(frames, (block + 1) * 128); ++i)
                {
                    expected = waveform::combine(expected,
                                                 {sample(c, i), sample(c, i)});
                }
                uint64_t visited = 0;
                auto actual = peaks->queryBlocks(c, block, block + 1, visited);
                CHECK(actual.min == expected.min);
                CHECK(actual.max == expected.max);
            }
        }
    }
}

TEST_CASE(
    "Failed sample reads poison peak transactions and cancellation prevents "
    "publication",
    "[streaming-peaks]")
{
    storage::AudioShape shape{128 * 4097, 2, 48000, SampleFormat::FLOAT32};
    waveform::StreamingPeakBuilder failed(shape);
    CHECK_THROWS(failed.appendFrom(shape, 65536,
                                   [](int c, int64_t, std::span<float> out)
                                   {
                                       if (c)
                                       {
                                           throw std::runtime_error(
                                               "read failure");
                                       }
                                       std::fill(out.begin(), out.end(), .5f);
                                   }));
    CHECK_THROWS(failed.finish());
    CHECK_THROWS(failed.appendFrom(shape, 65536, {}));
    bool cancel = false;
    waveform::StreamingPeakBuilder canceled(shape, {},
                                            [&]
                                            {
                                                return cancel;
                                            });
    canceled.appendFrom(shape, shape.frames,
                        [](int, int64_t, std::span<float> out)
                        {
                            std::fill(out.begin(), out.end(), 0);
                        });
    cancel = true;
    CHECK_THROWS_AS(canceled.finish(), LongTaskCanceledError);
}

TEST_CASE("A cancellation in the final base-peak read prevents publication",
          "[streaming-peaks]")
{
    bool cancel = false;
    CHECK_THROWS_AS(waveform::SourcePeaks::createStreaming(
                        {128, 1, 48000, SampleFormat::FLOAT32},
                        [&](int, uint64_t, std::span<waveform::Peak> out)
                        {
                            std::fill(out.begin(), out.end(),
                                      waveform::Peak{0, 0});
                            cancel = true;
                        },
                        {},
                        [&]
                        {
                            return cancel;
                        }),
                    LongTaskCanceledError);
}

TEST_CASE("Streaming peak reduction preserves leading NaNs and signed zeros",
          "[streaming-peaks]")
{
    storage::AudioShape shape{256, 1, 48000, SampleFormat::FLOAT32};
    waveform::StreamingPeakBuilder builder(shape);
    std::array<float, 256> samples{};
    samples[0] = std::numeric_limits<float>::quiet_NaN();
    samples[1] = 1;
    samples[128] = -0.0f;
    builder.appendFrom(shape, 1,
                       [&](int, int64_t at, std::span<float> out)
                       {
                           std::copy_n(samples.data() + at, out.size(),
                                       out.data());
                       });
    builder.appendFrom(shape, 256,
                       [&](int, int64_t at, std::span<float> out)
                       {
                           std::copy_n(samples.data() + at, out.size(),
                                       out.data());
                       });
    auto peaks = builder.finish();
    std::array<waveform::Peak, 2> base;
    peaks->readPeaks(0, 0, 0, base);
    CHECK(std::isnan(base[0].min));
    CHECK(std::isnan(base[0].max));
    CHECK(std::signbit(base[1].min));
    CHECK(std::signbit(base[1].max));
}
