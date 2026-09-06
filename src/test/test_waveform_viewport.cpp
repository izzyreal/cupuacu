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
