#include <sndfile.h>
#include "LongTask.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestPaths.hpp"
#include "TestResourceUtil.hpp"
#include "concurrency/TaskScheduler.hpp"
#include "file/LegacyAudioLoading.hpp"
#include "file/OwnedAudioImport.hpp"
#include "actions/io/BackgroundOpen.hpp"
#include "gui/WaveformOverviewPlanning.hpp"
#include "waveform/WaveformCachePersistence.hpp"
#include "waveform/StreamingPeakBuilder.hpp"
#include "waveform/ProgressivePeaks.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <thread>
#include <vector>

namespace
{
    void initializeMonoDocument(cupuacu::DocumentSession &session,
                                const std::vector<float> &samples)
    {
        auto &document = session.document;
        document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1,
                            static_cast<int64_t>(samples.size()));
        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            document.setSample(0, static_cast<int64_t>(i), samples[i], false);
        }
    }

    void requireBuildStatesEqual(
        const cupuacu::gui::WaveformCache::BuildState &actual,
        const cupuacu::gui::WaveformCache::BuildState &expected)
    {
        REQUIRE(actual.numSamples == expected.numSamples);
        REQUIRE(actual.dirtyFromBlock == expected.dirtyFromBlock);
        REQUIRE(actual.dirtyToBlock == expected.dirtyToBlock);
        REQUIRE(actual.levels.size() == expected.levels.size());
        for (std::size_t levelIndex = 0; levelIndex < actual.levels.size();
             ++levelIndex)
        {
            const auto &actualLevel = actual.levels[levelIndex];
            const auto &expectedLevel = expected.levels[levelIndex];
            REQUIRE(actualLevel.size() == expectedLevel.size());
            for (std::size_t peakIndex = 0; peakIndex < actualLevel.size();
                 ++peakIndex)
            {
                REQUIRE(actualLevel[peakIndex].min ==
                        Catch::Approx(expectedLevel[peakIndex].min));
                REQUIRE(actualLevel[peakIndex].max ==
                        Catch::Approx(expectedLevel[peakIndex].max));
            }
        }
    }
} // namespace

TEST_CASE(
    "Streaming waveform previews match complete peaks across partial blocks",
    "[waveform][progressive-open][streaming-peaks]")
{
    using namespace cupuacu;
    constexpr int64_t frames = 196625;
    const storage::AudioShape shape{frames, 1, 44100, SampleFormat::FLOAT32};
    std::vector<float> samples(frames);
    for (int64_t i = 0; i < frames; ++i)
    {
        samples[i] = 0.1f + float(i % 317) / 1000.0f;
    }
    auto cache = std::make_shared<storage::DecodedBlockCache>(
        storage::AudioBlockBytes);
    auto live = std::make_shared<waveform::ProgressivePeaks>(shape, cache);
    waveform::StreamingPeakBuilder builder(shape, cache, {}, live);
    for (int64_t start = 0; start < frames; start += 1023)
    {
        const auto end = std::min<int64_t>(start + 1023, frames);
        builder.appendFrom(shape, end,
            [&](int channel, int64_t first, std::span<float> output)
            {
                REQUIRE(channel == 0);
                REQUIRE(first >= start);
                REQUIRE(first + int64_t(output.size()) <= end);
                std::copy_n(samples.data() + first, output.size(), output.data());
            });
        const auto available = live->availableFrames();
        REQUIRE(available == (end == frames ? frames : end / 128 * 128));
        const auto blocks = (available + 127) / 128;
        const auto peak = live->queryBlocks(0, blocks - 1, blocks);
        const auto first = (blocks - 1) * 128;
        const auto range = std::minmax_element(samples.begin() + first,
                                               samples.begin() + available);
        REQUIRE(peak.min == *range.first);
        REQUIRE(peak.max == *range.second);
    }
    gui::WaveformCache expected;
    expected.rebuildAll(samples.data(), frames);
    const auto result = builder.finish();
    const auto levels = expected.snapshotBuildState().levels;
    for (std::size_t level = 0; level < levels.size(); ++level)
    {
        std::vector<waveform::Peak> actual(levels[level].size());
        REQUIRE(result->levelSize(0, level) == actual.size());
        result->readPeaks(0, level, 0, actual);
        for (std::size_t i = 0; i < actual.size(); ++i)
        {
            REQUIRE(actual[i].min == levels[level][i].min);
            REQUIRE(actual[i].max == levels[level][i].max);
        }
    }
}

TEST_CASE("Imported peak pages persist and reopen without full peak snapshots",
          "[progressive-peaks][peak-persistence]")
{
    using namespace cupuacu;
    const auto root = test::makeUniqueTestRoot("paged-import-cache");
    std::filesystem::create_directories(root);
    const auto path = root / "audio.wav";
    SF_INFO info{};
    info.channels = 2;
    info.samplerate = 48000;
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    auto *file = sf_open(path.string().c_str(), SFM_WRITE, &info);
    REQUIRE(file);
    std::vector<float> samples(65536 * 2);
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        samples[i] = float(int(i % 199) - 99) / 128;
    }
    for (int i = 0; i < 35; ++i)
    {
        REQUIRE(sf_writef_float(file, samples.data(), 65536) == 65536);
    }
    REQUIRE(sf_writef_float(file, samples.data(), 17) == 17);
    REQUIRE(sf_close(file) == 0);
    auto cache =
        std::make_shared<storage::DecodedBlockCache>(storage::AudioBlockBytes);
    file::OwnedImportOptions options;
    options.waveformCacheRoot = root / "cache";
    options.publishMetadata = options.publishAudio = true;
    auto first = file::importOwnedAudio(path, root / "first", cache, {}, {}, {},
                                        options);
    REQUIRE_FALSE(first.metadata.persistentWaveformCacheLoaded);
    REQUIRE(first.metadata.pendingImportedPeaks);
    REQUIRE(first.metadata.pendingImportedPeaks->channels.empty());
    REQUIRE(waveform::schedulePersistentWaveformCache(
                first.metadata.pendingImportedPeaks) ==
            waveform::CacheSaveScheduleResult::Scheduled);
    waveform::flushScheduledPersistentWaveformCaches();
    bool cachedPreview = false;
    auto reopened = file::importOwnedAudio(
        path, root / "second", cache, {}, {},
        [&](waveform::ImportPreview chunk)
        {
            if (!chunk.sourcePeaks)
            {
                return;
            }
            cachedPreview = true;
            uint64_t visited = 0;
            CHECK(chunk.sourcePeaks->queryBlocks(1, 0, 513, visited).max > 0);
        },
        options);
    REQUIRE(cachedPreview);
    REQUIRE(reopened.metadata.persistentWaveformCacheLoaded);
    REQUIRE_FALSE(reopened.metadata.pendingImportedPeaks);
    const auto baseCount = first.audio->sourcePeaks()->levelSize(0, 0);
    for (int c = 0; c < 2; ++c)
    {
        for (std::size_t l = 0, n = baseCount;; ++l, n = (n + 1) / 2)
        {
            std::vector<waveform::Peak> expected(n), actual(n);
            first.audio->sourcePeaks()->readPeaks(c, l, 0, expected);
            reopened.audio->sourcePeaks()->readPeaks(c, l, 0, actual);
            for (std::size_t i = 0; i < n; ++i)
            {
                REQUIRE(actual[i].min == expected[i].min);
                REQUIRE(actual[i].max == expected[i].max);
            }
            if (n == 1)
            {
                break;
            }
        }
    }
    DocumentSession legacy;
    legacy.document = first.metadata.document;
    legacy.currentFile = path.string();
    REQUIRE(waveform::loadPersistentWaveformCache(legacy,
                                                  options.waveformCacheRoot));
    // The streamed loader also accepts existing v1 files, and rejects a
    // truncated tail before exposing any cached preview.
    REQUIRE(waveform::savePersistentWaveformCache(legacy,
                                                  options.waveformCacheRoot));
    REQUIRE(waveform::loadPersistentSourcePeaks(
        path.string(), legacy.document, options.waveformCacheRoot, cache));
    const auto cachePath =
        options.waveformCacheRoot /
        first.metadata.pendingImportedPeaks->key.cacheBasename();
    std::filesystem::resize_file(cachePath,
                                 std::filesystem::file_size(cachePath) - 1);
    REQUIRE_FALSE(waveform::loadPersistentSourcePeaks(
        path.string(), legacy.document, options.waveformCacheRoot, cache));
    REQUIRE_FALSE(waveform::loadPersistentSourcePeaks(
        path.string(), legacy.document, options.waveformCacheRoot, cache,
        []
        {
            return true;
        }));
    auto fallback = file::importOwnedAudio(path, root / "fallback", cache, {},
                                           {}, {}, options);
    REQUIRE_FALSE(fallback.metadata.persistentWaveformCacheLoaded);
    REQUIRE(fallback.audio->sourcePeaks());
}

TEST_CASE(
    "Progressive open publishes early and cancellation restores the previous "
    "document",
    "[waveform][progressive-open]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("progressive-open");
    std::filesystem::create_directories(root);
    const auto path = root / "source.wav";
    SF_INFO info{};
    info.channels = 1;
    info.samplerate = 44100;
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    auto *file = sf_open(path.string().c_str(), SFM_WRITE, &info);
    REQUIRE(file != nullptr);
    std::vector<float> block(65536, 0.25f);
    for (int i = 0; i < 64; ++i)
    {
        REQUIRE(sf_writef_float(file, block.data(), block.size()) ==
                block.size());
    }
    sf_close(file);

    cupuacu::test::StateWithTestPaths state{root};
    state.getActiveDocumentSession().document.initialize(
        cupuacu::SampleFormat::FLOAT32, 48000, 1, 23);
    state.getActiveDocumentSession().document.setSample(0, 0, 0.75f);
    cupuacu::actions::io::queueOpenFile(&state, path.string());
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((!state.getActiveDocumentSession().openingPreview ||
            !state.getActiveDocumentSession().openingPeaks ||
            state.getActiveDocumentSession().openingPeaks->availableFrames() ==
                0) &&
           std::chrono::steady_clock::now() < deadline)
    {
        cupuacu::actions::io::processPendingOpenWork(&state);
        std::this_thread::yield();
    }
    REQUIRE(state.getActiveDocumentSession().openingPreview);
    REQUIRE(state.getActiveDocumentSession().openingPeaks);
    REQUIRE(state.getActiveDocumentSession().openingPeaks->availableFrames() >
            0);
    REQUIRE(state.backgroundOpenJob);
    cupuacu::requestLongTaskCancel(&state);
    while (state.backgroundOpenJob &&
           std::chrono::steady_clock::now() < deadline)
    {
        cupuacu::actions::io::processPendingOpenWork(&state);
        std::this_thread::yield();
    }
    REQUIRE_FALSE(state.backgroundOpenJob);
    REQUIRE_FALSE(state.longTask.active);
    REQUIRE(state.tabs.size() == 1);
    REQUIRE(state.getActiveDocumentSession().document.getFrameCount() == 23);
    REQUIRE(state.getActiveDocumentSession().document.getSample(0, 0) == 0.75f);
}

TEST_CASE("Waveform writes use bounded maintenance admission and drain on shutdown",
          "[waveform][persistence][concurrency]")
{
    using namespace cupuacu;
    waveform::flushScheduledPersistentWaveformCaches();
    auto scheduler = std::make_shared<cupuacu::concurrency::TaskScheduler>(1, 8);
    std::promise<void> started, release;
    auto gate = release.get_future().share();
    auto active = started.get_future();
    auto blocker = scheduler->submit([&] { started.set_value(); gate.wait(); }, {});
    active.wait();
    std::vector<int> order;
    std::vector<waveform::CacheSaveScheduleResult> accepted;
    for (int i = 0; i < 5; ++i)
    {
        auto snapshot = std::make_shared<waveform::PersistentCacheSnapshot>();
        snapshot->beforeWrite = [&, i]
        {
            order.push_back(i);
            throw std::runtime_error("Injected disposable cache failure");
        };
        accepted.push_back(waveform::schedulePersistentWaveformCache(snapshot, scheduler));
    }
    const auto overflow = waveform::schedulePersistentWaveformCache(
        std::make_shared<waveform::PersistentCacheSnapshot>(), scheduler);
    auto user = scheduler->submit([&] { order.push_back(-1); }, {});
    const bool busy = waveform::hasScheduledPersistentWaveformCacheWork();
    release.set_value();
    SECTION("Explicit flush drains accepted writes")
    {
        waveform::flushScheduledPersistentWaveformCaches();
    }
    SECTION("Scheduler shutdown drains accepted writes")
    {
        scheduler.reset();
    }
    user.get();
    REQUIRE(busy);
    REQUIRE_FALSE(waveform::hasScheduledPersistentWaveformCacheWork());
    for (auto result : accepted)
    {
        REQUIRE(result == waveform::CacheSaveScheduleResult::Scheduled);
    }
    REQUIRE(overflow == waveform::CacheSaveScheduleResult::Busy);
    REQUIRE(order == std::vector<int>{-1, 0, 1, 2, 3, 4});
}

TEST_CASE("Scheduler rejection releases waveform admission for retry",
          "[waveform][persistence][concurrency]")
{
    using namespace cupuacu;
    waveform::flushScheduledPersistentWaveformCaches();
    auto scheduler = std::make_shared<cupuacu::concurrency::TaskScheduler>(1, 1);
    std::promise<void> started, release;
    auto gate = release.get_future().share();
    auto active = started.get_future();
    auto blocker = scheduler->submit([&] { started.set_value(); gate.wait(); }, {});
    active.wait();
    auto snapshot = std::make_shared<waveform::PersistentCacheSnapshot>();
    snapshot->beforeWrite = [] { throw std::runtime_error("Injected failure"); };
    const auto first = waveform::schedulePersistentWaveformCache(snapshot, scheduler);
    const auto rejected = waveform::schedulePersistentWaveformCache(snapshot, scheduler);
    release.set_value();
    waveform::flushScheduledPersistentWaveformCaches();
    blocker = {};
    // Shutdown proves rejected work retained neither scheduler nor admission.
    scheduler.reset();
    scheduler = std::make_shared<cupuacu::concurrency::TaskScheduler>(1, 1);
    REQUIRE(first == waveform::CacheSaveScheduleResult::Scheduled);
    REQUIRE(rejected == waveform::CacheSaveScheduleResult::Busy);
    REQUIRE(waveform::schedulePersistentWaveformCache(snapshot, scheduler) ==
            waveform::CacheSaveScheduleResult::Scheduled);
    waveform::flushScheduledPersistentWaveformCaches();
    REQUIRE_FALSE(waveform::hasScheduledPersistentWaveformCacheWork());
}

TEST_CASE(
    "Queued waveform persistence owns peaks without retaining document audio",
    "[waveform][persistence]")
{
    cupuacu::test::StateWithTestPaths state{
        cupuacu::test::makeUniqueTestRoot("queued-waveform-cache")};
    const auto sourcePath = state.paths->statePath() / "source.wav";
    std::filesystem::create_directories(sourcePath.parent_path());
    {
        std::ofstream output(sourcePath);
        output << "source";
    }
    auto &session = state.getActiveDocumentSession();
    initializeMonoDocument(session, {-0.25f, 0.5f, -0.75f, 0.25f});
    session.setCurrentFile(sourcePath.string());
    session.rebuildWaveformCacheSynchronously();
    const auto expected = session.getWaveformCache(0).snapshotBuildState();
    std::weak_ptr<cupuacu::audio::AudioBuffer> audio =
        session.document.getAudioBuffer();
    cupuacu::waveform::flushScheduledPersistentWaveformCaches();
    REQUIRE(cupuacu::waveform::schedulePersistentWaveformCache(session,
                                                               *state.paths) ==
            cupuacu::waveform::CacheSaveScheduleResult::Scheduled);

    initializeMonoDocument(session, {0, 0, 0, 0});
    session.rebuildWaveformCacheSynchronously();
    REQUIRE(audio.expired());
    cupuacu::waveform::flushScheduledPersistentWaveformCaches();
    REQUIRE_FALSE(cupuacu::waveform::hasScheduledPersistentWaveformCacheWork());
    REQUIRE(
        cupuacu::waveform::loadPersistentWaveformCache(session, *state.paths));
    requireBuildStatesEqual(session.getWaveformCache(0).snapshotBuildState(),
                            expected);
}

TEST_CASE("A failed queued cache write does not prevent later saves",
          "[waveform][persistence]")
{
    cupuacu::test::StateWithTestPaths state{
        cupuacu::test::makeUniqueTestRoot("failed-queued-cache")};
    const auto sourcePath = state.paths->statePath() / "source.wav";
    std::filesystem::create_directories(sourcePath.parent_path());
    {
        std::ofstream output(sourcePath);
        output << "source";
    }
    auto &session = state.getActiveDocumentSession();
    initializeMonoDocument(session, {0, 1, 0, -1});
    session.setCurrentFile(sourcePath.string());
    session.rebuildWaveformCacheSynchronously();
    const auto root = state.paths->waveformCachePath();
    {
        std::ofstream output(root);
        output << "blocks directory creation";
    }
    cupuacu::waveform::flushScheduledPersistentWaveformCaches();
    REQUIRE(cupuacu::waveform::schedulePersistentWaveformCache(session,
                                                               *state.paths) ==
            cupuacu::waveform::CacheSaveScheduleResult::Scheduled);
    cupuacu::waveform::flushScheduledPersistentWaveformCaches();
    REQUIRE_FALSE(std::filesystem::exists(
        session.getPersistentWaveformCachePath(*state.paths)));
    std::filesystem::remove(root);
    REQUIRE(cupuacu::waveform::schedulePersistentWaveformCache(session,
                                                               *state.paths) ==
            cupuacu::waveform::CacheSaveScheduleResult::Scheduled);
    cupuacu::waveform::flushScheduledPersistentWaveformCaches();
    REQUIRE(
        cupuacu::waveform::loadPersistentWaveformCache(session, *state.paths));
}

TEST_CASE("A deferred waveform cache save is invalidated by audio edits",
          "[waveform][persistence]")
{
    cupuacu::test::StateWithTestPaths state{
        cupuacu::test::makeUniqueTestRoot("deferred-cache-edit")};
    const auto sourcePath = state.paths->statePath() / "source.wav";
    std::filesystem::create_directories(sourcePath.parent_path());
    {
        std::ofstream output(sourcePath);
        output << "source";
    }
    auto &session = state.getActiveDocumentSession();
    initializeMonoDocument(session, {0, 1, 0, -1});
    session.setCurrentFile(sourcePath.string());
    session.markPendingPersistentWaveformCacheSave();
    session.document.setSample(0, 0, 0.5f);
    session.rebuildWaveformCacheSynchronously();
    (void)session.pumpWaveformCacheWork(state.paths.get());
    cupuacu::waveform::flushScheduledPersistentWaveformCaches();
    REQUIRE_FALSE(session.pendingPersistentWaveformCacheVersion);
    REQUIRE_FALSE(std::filesystem::exists(
        session.getPersistentWaveformCachePath(*state.paths)));
}

TEST_CASE("Waveform cache persistence key uses source file metadata and document shape",
          "[waveform][persistence]")
{
    const auto root =
        cupuacu::test::makeUniqueTestRoot("waveform-cache-persistence");
    cupuacu::test::StateWithTestPaths state{root};

    const auto sourcePath = root / "source.wav";
    std::filesystem::create_directories(sourcePath.parent_path());
    {
        std::ofstream output(sourcePath, std::ios::binary);
        REQUIRE(output.is_open());
        output << "abc";
    }

    auto &session = state.getActiveDocumentSession();
    initializeMonoDocument(session, {0.25f, -0.5f, 0.75f});
    session.setCurrentFile(sourcePath.string());

    const auto key = session.getPersistentWaveformCacheKey();
    REQUIRE(key.has_value());
    REQUIRE(key->sourceFileSize == 3);
    REQUIRE(key->sampleRate == 44100);
    REQUIRE(key->channelCount == 1);
    REQUIRE(key->frameCount == 3);

    const auto cachePath = session.getPersistentWaveformCachePath(*state.paths);
    REQUIRE(cachePath.parent_path() == state.paths->waveformCachePath());
    REQUIRE(cachePath.filename() == key->cacheBasename());

    {
        std::ofstream output(sourcePath, std::ios::binary | std::ios::app);
        REQUIRE(output.is_open());
        output << 'd';
    }

    const auto fileChangedKey = session.getPersistentWaveformCacheKey();
    REQUIRE(fileChangedKey.has_value());
    REQUIRE(fileChangedKey->sourceFileSize == 4);
    REQUIRE(fileChangedKey->cacheBasename() != key->cacheBasename());

    session.document.insertFrames(session.document.getFrameCount(), 1);
    const auto documentChangedKey = session.getPersistentWaveformCacheKey();
    REQUIRE(documentChangedKey.has_value());
    REQUIRE(documentChangedKey->frameCount == 4);
    REQUIRE(documentChangedKey->cacheBasename() != fileChangedKey->cacheBasename());
}

TEST_CASE("Waveform cache persistence key is unavailable without a backed file",
          "[waveform][persistence]")
{
    cupuacu::DocumentSession session;
    initializeMonoDocument(session, {0.0f, 1.0f});

    REQUIRE_FALSE(session.getPersistentWaveformCacheKey().has_value());
    REQUIRE(session.getPersistentWaveformCachePath(cupuacu::Paths{}).empty());

    session.setCurrentFile("/path/that/does/not/exist.wav");
    REQUIRE_FALSE(session.getPersistentWaveformCacheKey().has_value());
}

TEST_CASE("Waveform cache persistence round-trips clean built peaks",
          "[waveform][persistence]")
{
    const auto root =
        cupuacu::test::makeUniqueTestRoot("waveform-cache-persistence");
    cupuacu::test::StateWithTestPaths state{root};

    const auto sourcePath = root / "source.wav";
    std::filesystem::create_directories(sourcePath.parent_path());
    {
        std::ofstream output(sourcePath, std::ios::binary);
        REQUIRE(output.is_open());
        output << "waveform";
    }

    auto &session = state.getActiveDocumentSession();
    initializeMonoDocument(session, {-0.25f, 0.5f, -0.75f, 0.25f, 0.75f, -0.5f});
    session.setCurrentFile(sourcePath.string());
    session.rebuildWaveformCacheSynchronously();

    REQUIRE(cupuacu::waveform::savePersistentWaveformCache(session, *state.paths));

    cupuacu::DocumentSession restored;
    initializeMonoDocument(restored, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
    restored.setCurrentFile(sourcePath.string());

    REQUIRE(cupuacu::waveform::loadPersistentWaveformCache(restored,
                                                           *state.paths));

    const auto originalState = session.getWaveformCache(0).snapshotBuildState();
    const auto restoredState = restored.getWaveformCache(0).snapshotBuildState();
    requireBuildStatesEqual(restoredState, originalState);
}

TEST_CASE("Waveform cache persistence rejects stale source metadata",
          "[waveform][persistence]")
{
    const auto root =
        cupuacu::test::makeUniqueTestRoot("waveform-cache-persistence");
    cupuacu::test::StateWithTestPaths state{root};

    const auto sourcePath = root / "source.wav";
    std::filesystem::create_directories(sourcePath.parent_path());
    {
        std::ofstream output(sourcePath, std::ios::binary);
        REQUIRE(output.is_open());
        output << "abc";
    }

    auto &session = state.getActiveDocumentSession();
    initializeMonoDocument(session, {-0.25f, 0.5f, -0.75f});
    session.setCurrentFile(sourcePath.string());
    session.rebuildWaveformCacheSynchronously();
    REQUIRE(cupuacu::waveform::savePersistentWaveformCache(session, *state.paths));

    {
        std::ofstream output(sourcePath, std::ios::binary | std::ios::app);
        REQUIRE(output.is_open());
        output << 'd';
    }

    cupuacu::DocumentSession restored;
    initializeMonoDocument(restored, {-0.25f, 0.5f, -0.75f});
    restored.setCurrentFile(sourcePath.string());

    REQUIRE_FALSE(cupuacu::waveform::loadPersistentWaveformCache(
        restored, *state.paths));
}

TEST_CASE("Synchronous file open persists and reuses the initial waveform cache",
          "[waveform][persistence]")
{
    const auto root =
        cupuacu::test::makeUniqueTestRoot("waveform-cache-persistence");
    const auto sourcePath = root / "FINGER_CYM1.WAV";
    cupuacu::test::write_test_resource_file("FINGER_CYM1.WAV", sourcePath);

    {
        cupuacu::test::StateWithTestPaths state{root};
        auto &session = state.getActiveDocumentSession();
        session.setCurrentFile(sourcePath.string());
        cupuacu::file::legacy::loadSampleData(&state);

        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline)
        {
            (void)session.pumpWaveformCacheWork(state.paths.get());
            if (!session.getWaveformCacheBuildProgress().has_value())
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        REQUIRE_FALSE(session.getWaveformCacheBuildProgress().has_value());
        cupuacu::waveform::flushScheduledPersistentWaveformCaches();
        const auto cachePath =
            session.getPersistentWaveformCachePath(*state.paths);
        REQUIRE_FALSE(cachePath.empty());
        REQUIRE(std::filesystem::exists(cachePath));
    }

    {
        cupuacu::test::StateWithTestPaths state{root};
        auto &session = state.getActiveDocumentSession();
        session.setCurrentFile(sourcePath.string());
        cupuacu::file::legacy::loadSampleData(&state);

        REQUIRE_FALSE(session.getWaveformCacheBuildProgress().has_value());
        const auto cacheState = session.getWaveformCache(0).snapshotBuildState();
        REQUIRE(cacheState.dirtyToBlock < cacheState.dirtyFromBlock);
        REQUIRE_FALSE(cacheState.levels.empty());
    }
}
