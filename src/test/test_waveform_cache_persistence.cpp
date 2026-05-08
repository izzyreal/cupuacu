#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestPaths.hpp"
#include "TestResourceUtil.hpp"
#include "file/file_loading.hpp"
#include "waveform/WaveformCachePersistence.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
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
