#include <catch2/catch_test_macros.hpp>

#include "IntegrationTestHelpers.hpp"

#include "State.hpp"
#include "actions/io/BackgroundOpen.hpp"
#include "actions/ShowOpenFileDialog.hpp"
#include "file/SndfilePath.hpp"
#include "gui/DevicePropertiesWindow.hpp"

#include <sndfile.h>

#include <chrono>
#include <thread>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    class ScopedDirCleanup
    {
    public:
        explicit ScopedDirCleanup(std::filesystem::path rootDir)
            : root(std::move(rootDir))
        {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
            std::filesystem::create_directories(root, ec);
        }

        ~ScopedDirCleanup()
        {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }

        const std::filesystem::path &path() const
        {
            return root;
        }

    private:
        std::filesystem::path root;
    };

    std::filesystem::path makeUniqueTempDir(const std::string &prefix)
    {
        const auto tempRoot = std::filesystem::temp_directory_path();
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dis;

        for (int attempt = 0; attempt < 32; ++attempt)
        {
            const auto now =
                std::chrono::high_resolution_clock::now().time_since_epoch();
            const auto tick =
                static_cast<uint64_t>(std::chrono::duration_cast<
                                          std::chrono::nanoseconds>(now)
                                          .count());
            const uint64_t nonce = dis(gen);
            const auto candidate =
                tempRoot / (prefix + "-" + std::to_string(tick) + "-" +
                            std::to_string(nonce));
            std::error_code ec;
            if (!std::filesystem::exists(candidate, ec))
            {
                return candidate;
            }
        }

        return tempRoot / (prefix + "-fallback");
    }

    void writeTestWav(const std::filesystem::path &path, const int sampleRate,
                      const int channels,
                      const std::vector<float> &interleavedFrames)
    {
        REQUIRE(channels > 0);
        REQUIRE(interleavedFrames.size() % channels == 0);

        SF_INFO info{};
        info.samplerate = sampleRate;
        info.channels = channels;
        info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

        SNDFILE *file = cupuacu::file::openSndfile(path, SFM_WRITE, &info);
        REQUIRE(file != nullptr);

        const sf_count_t frameCount =
            static_cast<sf_count_t>(interleavedFrames.size() / channels);
        const sf_count_t written =
            sf_writef_float(file, interleavedFrames.data(), frameCount);

        sf_close(file);
        REQUIRE(written == frameCount);
    }
} // namespace

TEST_CASE("Open file dialog callback queues the selected file",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-open-dialog"));
    const auto wavPath = cleanup.path() / "selected.wav";
    writeTestWav(wavPath, 22050, 2,
                 {0.25f, -0.25f, 0.5f, -0.5f, 0.75f, -0.75f});

    cupuacu::test::StateWithTestPaths state{};
    auto sessionUi =
        cupuacu::test::integration::createSessionUi(&state, 32, false, 1);

    auto &originalSession = state.getActiveDocumentSession();
    originalSession.currentFile = "before.wav";
    originalSession.selection.setHighest(8.0);
    originalSession.selection.setValue1(3.0);
    originalSession.selection.setValue2(8.0);
    originalSession.cursor = 7;

    auto &originalViewState = state.getActiveViewState();
    originalViewState.verticalZoom = 4;
    originalViewState.sampleOffset = 9;
    originalViewState.samplesToScroll = 3;
    originalViewState.selectedChannels = cupuacu::SelectedChannels::LEFT;

    const std::string pathString = wavPath.string();
    const char *selectedFiles[] = {pathString.c_str(), nullptr};

    cupuacu::actions::fileDialogCallback(&state, selectedFiles, 0);

    REQUIRE(state.pendingOpenFiles.size() == 1);
    REQUIRE(state.pendingOpenFiles.front().kind ==
            cupuacu::PendingOpenKind::UserOpen);
    REQUIRE(state.pendingOpenFiles.front().path == pathString);
    REQUIRE(state.pendingOpenFiles.front().updateRecentFiles);
    REQUIRE(state.tabs.size() == 1);
    REQUIRE(state.getActiveDocumentSession().currentFile == "before.wav");
    REQUIRE(state.getActiveViewState().verticalZoom == 4);
    REQUIRE(state.getActiveViewState().sampleOffset == 9);
}

TEST_CASE("Open file dialog callback queues multiple selected files",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-open-dialog-multiple"));
    const auto firstWavPath = cleanup.path() / "first.wav";
    const auto secondWavPath = cleanup.path() / "second.wav";
    writeTestWav(firstWavPath, 44100, 1, {0.1f, 0.2f});
    writeTestWav(secondWavPath, 22050, 2, {0.25f, -0.25f, 0.5f, -0.5f});

    cupuacu::test::StateWithTestPaths state{};
    auto sessionUi =
        cupuacu::test::integration::createSessionUi(&state, 32, false, 1);

    const std::string firstPathString = firstWavPath.string();
    const std::string secondPathString = secondWavPath.string();
    const char *selectedFiles[] = {firstPathString.c_str(),
                                   secondPathString.c_str(), nullptr};

    cupuacu::actions::fileDialogCallback(&state, selectedFiles, 0);

    REQUIRE(state.pendingOpenFiles.size() == 2);
    REQUIRE(state.pendingOpenFiles[0].kind ==
            cupuacu::PendingOpenKind::UserOpen);
    REQUIRE(state.pendingOpenFiles[0].path == firstPathString);
    REQUIRE(state.pendingOpenFiles[1].kind ==
            cupuacu::PendingOpenKind::UserOpen);
    REQUIRE(state.pendingOpenFiles[1].path == secondPathString);
    REQUIRE(state.tabs.size() == 1);
    REQUIRE(state.recentFiles.empty());
}

TEST_CASE("Pending open work loads queued dialog files asynchronously",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-open-async"));
    const auto wavPath = cleanup.path() / "async.wav";
    writeTestWav(wavPath, 32000, 2,
                 {0.125f, -0.125f, 0.25f, -0.25f});

    cupuacu::test::StateWithTestPaths state{};
    auto sessionUi =
        cupuacu::test::integration::createSessionUi(&state, 32, false, 1);

    const std::string pathString = wavPath.string();
    const char *selectedFiles[] = {pathString.c_str(), nullptr};
    cupuacu::actions::fileDialogCallback(&state, selectedFiles, 0);

    for (int attempt = 0; attempt < 200; ++attempt)
    {
        cupuacu::actions::io::processPendingOpenWork(&state);
        state.getActiveDocumentSession().document.pumpWaveformCacheWork();
        for (auto *window : state.windows)
        {
            if (window && window->isOpen() && window->getRootComponent())
            {
                window->getRootComponent()->timerCallbackRecursive();
            }
        }
        if (state.pendingOpenFiles.empty() && !state.backgroundOpenJob &&
            !state.pendingOpenWaveformBuild.active &&
            state.getActiveDocumentSession().currentFile == pathString)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    REQUIRE(state.pendingOpenFiles.empty());
    REQUIRE_FALSE(state.backgroundOpenJob);
    REQUIRE_FALSE(state.pendingOpenWaveformBuild.active);
    REQUIRE(state.getActiveDocumentSession().currentFile == pathString);
    REQUIRE(state.getActiveDocumentSession().document.getSampleRate() == 32000);
    REQUIRE(state.getActiveDocumentSession().document.getFrameCount() == 2);
    REQUIRE(state.getActiveDocumentSession().document.getSample(0, 1) == 0.25f);
    REQUIRE(state.recentFiles == std::vector<std::string>{pathString});
}

TEST_CASE("Pending open work commits the document before waveform cache build completes",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-open-progressive-cache"));
    const auto wavPath = cleanup.path() / "progressive-cache.wav";
    constexpr int sampleRate = 44100;
    constexpr int channels = 1;
    constexpr int64_t frameCount = 1 << 22;
    std::vector<float> frames(static_cast<std::size_t>(frameCount));
    for (int64_t frame = 0; frame < frameCount; ++frame)
    {
        frames[static_cast<std::size_t>(frame)] =
            (frame % 128) < 64 ? -0.5f : 0.5f;
    }
    writeTestWav(wavPath, sampleRate, channels, frames);

    cupuacu::test::StateWithTestPaths state{};
    auto sessionUi =
        cupuacu::test::integration::createSessionUi(&state, 32, false, 1);

    const std::string pathString = wavPath.string();
    const char *selectedFiles[] = {pathString.c_str(), nullptr};
    cupuacu::actions::fileDialogCallback(&state, selectedFiles, 0);

    bool sawCommittedWhileBuilding = false;
    std::vector<double> buildProgressValues;
    for (int attempt = 0; attempt < 5000; ++attempt)
    {
        cupuacu::actions::io::processPendingOpenWork(&state);
        state.getActiveDocumentSession().document.pumpWaveformCacheWork();
        for (auto *window : state.windows)
        {
            if (window && window->isOpen() && window->getRootComponent())
            {
                window->getRootComponent()->timerCallbackRecursive();
            }
        }

        if (state.getActiveDocumentSession().currentFile == pathString &&
            state.pendingOpenWaveformBuild.active)
        {
            sawCommittedWhileBuilding = true;
            if (state.longTask.progress.has_value())
            {
                buildProgressValues.push_back(*state.longTask.progress);
            }
        }

        if (state.pendingOpenFiles.empty() && !state.backgroundOpenJob &&
            !state.pendingOpenWaveformBuild.active &&
            state.getActiveDocumentSession().currentFile == pathString)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    REQUIRE(sawCommittedWhileBuilding);
    REQUIRE(buildProgressValues.size() >= 2);
    REQUIRE(std::is_sorted(buildProgressValues.begin(),
                           buildProgressValues.end()));
    REQUIRE(std::any_of(buildProgressValues.begin(), buildProgressValues.end(),
                        [](const double progress)
                        { return progress > 0.0 && progress < 1.0; }));
    REQUIRE(state.pendingOpenFiles.empty());
    REQUIRE_FALSE(state.backgroundOpenJob);
    REQUIRE_FALSE(state.pendingOpenWaveformBuild.active);
    REQUIRE_FALSE(state.longTask.active);
    REQUIRE(state.getActiveDocumentSession().currentFile == pathString);
}

TEST_CASE("Open file dialog callback leaves state unchanged when canceled",
          "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    auto sessionUi =
        cupuacu::test::integration::createSessionUi(&state, 12, false);

    auto &session = state.getActiveDocumentSession();
    session.currentFile = "unchanged.wav";
    session.selection.setHighest(4.0);
    session.selection.setValue1(1.0);
    session.selection.setValue2(4.0);
    session.cursor = 5;

    auto &viewState = state.getActiveViewState();
    viewState.verticalZoom = 3;
    viewState.sampleOffset = 6;

    const char *canceledFiles[] = {nullptr};
    cupuacu::actions::fileDialogCallback(&state, canceledFiles, 0);

    REQUIRE(session.currentFile == "unchanged.wav");
    REQUIRE(session.selection.isActive());
    REQUIRE(session.cursor == 5);
    REQUIRE(viewState.verticalZoom == 3);
    REQUIRE(viewState.sampleOffset == 6);
}

TEST_CASE("Open file dialog callback tolerates dialog errors", "[integration]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = "still-unchanged.wav";

    REQUIRE_NOTHROW(cupuacu::actions::fileDialogCallback(&state, nullptr, 0));
    REQUIRE(state.getActiveDocumentSession().currentFile == "still-unchanged.wav");
}
