#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestPaths.hpp"
#include "TestResourceUtil.hpp"
#include "concurrency/BoundedBackgroundWorker.hpp"
#include "file/file_loading.hpp"
#include "actions/io/BackgroundOpen.hpp"
#include "gui/WaveformOverviewPlanning.hpp"
#include "waveform/WaveformCachePersistence.hpp"

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
    "Decoded waveform chunks match a complete build across partial blocks",
    "[waveform][progressive-open]")
{
    constexpr int64_t frames = 196625;
    std::vector<float> samples(frames);
    for (int64_t i = 0; i < frames; ++i)
    {
        samples[i] = 0.1f + float(i % 317) / 1000.0f;
    }
    cupuacu::Document document;
    document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, frames);
    cupuacu::waveform::DecodedWaveformBuilder builder;
    cupuacu::DocumentSession preview;
    preview.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1,
                                frames);
    preview.openingPreview = true;
    int chunks = 0;
    for (int64_t start = 0; start < frames; start += 1023)
    {
        const auto count = std::min<int64_t>(1023, frames - start);
        document.writeChannelFloatBlock(0, start, samples.data() + start, count,
                                        false);
        if (auto chunk = builder.append(document, start + count))
        {
            ++chunks;
            preview.getWaveformCache(0).applyLevelSpanUpdates(
                frames, chunk->fromBlock, chunk->toBlock, chunk->channels[0]);
            REQUIRE(preview.getWaveformCache(0).builtSamplePrefixEnd() <=
                    start + count);
            cupuacu::gui::Peak peak{};
            cupuacu::gui::WaveformOverviewDebugStats stats;
            REQUIRE(cupuacu::gui::computeWaveformPeakForSampleWindow(
                preview, 0, 0, 1024, 1, 1, 1025, peak, &stats));
            REQUIRE(peak.min > 0); // Preview has no raw samples to read.
            REQUIRE(stats.rawSamplesScanned == 0);
        }
    }
    REQUIRE(chunks >= 3);
    cupuacu::gui::WaveformCache expected;
    expected.rebuildAll(samples.data(), frames);
    auto result = builder.takeCaches();
    requireBuildStatesEqual(result.getCache(0).snapshotBuildState(),
                            expected.snapshotBuildState());
    requireBuildStatesEqual(preview.getWaveformCache(0).snapshotBuildState(),
                            expected.snapshotBuildState());
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
    while (!state.getActiveDocumentSession().openingPreview &&
           std::chrono::steady_clock::now() < deadline)
    {
        cupuacu::actions::io::processPendingOpenWork(&state);
        std::this_thread::yield();
    }
    REQUIRE(state.getActiveDocumentSession().openingPreview);
    REQUIRE(state.getActiveDocumentSession()
                .getWaveformCache(0)
                .builtSamplePrefixEnd() > 0);
    REQUIRE(state.backgroundOpenJob);
    cupuacu::requestLongTaskCancel(&state);
    while (state.backgroundOpenJob &&
           std::chrono::steady_clock::now() < deadline)
    {
        cupuacu::actions::io::processPendingOpenWork(&state);
        std::this_thread::yield();
    }
    REQUIRE_FALSE(state.backgroundOpenJob);
    REQUIRE_FALSE(state.pendingOpenWaveformBuild.active);
    REQUIRE_FALSE(state.longTask.active);
    REQUIRE(state.tabs.size() == 1);
    REQUIRE(state.getActiveDocumentSession().document.getFrameCount() == 23);
    REQUIRE(state.getActiveDocumentSession().document.getSample(0, 0) == 0.75f);
}

TEST_CASE(
    "Bounded background work rejects overflow and drains retained requests",
    "[waveform][persistence][concurrency]")
{
    std::promise<void> started;
    std::promise<void> release;
    auto released = release.get_future().share();
    auto active = started.get_future();
    std::vector<int> processed;
    bool first = false, second = false, overflow = true, hadWork = false;
    bool didStart = false;
    {
        cupuacu::concurrency::BoundedBackgroundWorker<int, 1> worker(
            [&](const int &request)
            {
                if (request == 0)
                {
                    started.set_value();
                    released.wait();
                }
                processed.push_back(request);
            });
        first = worker.schedule(0);
        didStart = active.wait_for(std::chrono::seconds(5)) ==
                   std::future_status::ready;
        if (didStart)
        {
            second = worker.schedule(1);
            overflow = worker.schedule(2);
            hadWork = worker.hasWork();
        }
        // Release before assertions, so a test failure cannot strand a worker.
        release.set_value();
        SECTION("Flush waits for accepted requests")
        {
            worker.flush();
            REQUIRE_FALSE(worker.hasWork());
        }
        SECTION("Destruction waits for accepted requests") {}
    }
    REQUIRE(didStart);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE_FALSE(overflow);
    REQUIRE(hadWork);
    REQUIRE(processed == std::vector<int>{0, 1});
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
        cupuacu::file::loadSampleData(&state);

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
        cupuacu::file::loadSampleData(&state);

        REQUIRE_FALSE(session.getWaveformCacheBuildProgress().has_value());
        const auto cacheState = session.getWaveformCache(0).snapshotBuildState();
        REQUIRE(cacheState.dirtyToBlock < cacheState.dirtyFromBlock);
        REQUIRE_FALSE(cacheState.levels.empty());
    }
}
