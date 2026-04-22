#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "State.hpp"
#include "TestSdlTtfGuard.hpp"
#include "TestSdlLogSilencer.hpp"
#include "TestPaths.hpp"
#include "actions/DocumentLifecycle.hpp"
#include "file/SndfilePath.hpp"
#include "file/aiff/AiffMarkerMetadata.hpp"
#include "file/file_loading.hpp"
#include "file/wav/WavMarkerMetadata.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/DocumentSessionWindow.hpp"
#include "gui/Gui.hpp"
#include "gui/Window.hpp"

#include <sndfile.h>

#include <algorithm>
#include <chrono>
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

    void writeTestWavWithFormat(const std::filesystem::path &path,
                                const int sampleRate, const int channels,
                                const int format,
                                const std::vector<double> &interleavedFrames)
    {
        REQUIRE(channels > 0);
        REQUIRE(interleavedFrames.size() % channels == 0);

        SF_INFO info{};
        info.samplerate = sampleRate;
        info.channels = channels;
        info.format = format;

        SNDFILE *file = cupuacu::file::openSndfile(path, SFM_WRITE, &info);
        REQUIRE(file != nullptr);

        const sf_count_t frameCount =
            static_cast<sf_count_t>(interleavedFrames.size() / channels);
        const sf_count_t written =
            sf_writef_double(file, interleavedFrames.data(), frameCount);

        sf_close(file);
        REQUIRE(written == frameCount);
    }
} // namespace

TEST_CASE("Loading a file resets session selection and cursor", "[session]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-session-init-a"));

    const auto wavPath = cleanup.path() / "session_a.wav";
    const std::vector<float> samples = {
        0.1f, -0.1f, 0.2f, -0.2f, 0.3f, -0.3f, 0.4f, -0.4f};
    writeTestWav(wavPath, 48000, 2, samples);

    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();
    session.currentFile = wavPath.string();

    session.selection.setHighest(1'000'000.0);
    session.selection.setValue1(20.0);
    session.selection.setValue2(80.0);
    session.cursor = 77;
    REQUIRE(session.selection.isActive());
    REQUIRE(session.cursor == 77);

    cupuacu::file::loadSampleData(&state);

    REQUIRE(session.document.getSampleRate() == 48000);
    REQUIRE(session.document.getChannelCount() == 2);
    REQUIRE(session.document.getFrameCount() == 4);
    REQUIRE_FALSE(session.selection.isActive());
    REQUIRE(session.cursor == 0);

    REQUIRE(session.document.getSample(0, 0) == Catch::Approx(0.1f));
    REQUIRE(session.document.getSample(1, 0) == Catch::Approx(-0.1f));
    REQUIRE(session.document.getSample(0, 3) == Catch::Approx(0.4f));
    REQUIRE(session.document.getSample(1, 3) == Catch::Approx(-0.4f));
}

TEST_CASE("Loading a second file fully reinitializes document cache and shape",
          "[session]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-session-init-b"));

    const auto firstPath = cleanup.path() / "first.wav";
    const auto secondPath = cleanup.path() / "second.wav";

    const std::vector<float> firstSamples(2 * 1024, 0.25f);
    writeTestWav(firstPath, 44100, 2, firstSamples);

    std::vector<float> secondSamples(1 * 32, 0.0f);
    for (size_t i = 0; i < secondSamples.size(); ++i)
    {
        secondSamples[i] = static_cast<float>(i) / 64.0f;
    }
    writeTestWav(secondPath, 22050, 1, secondSamples);

    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();

    session.currentFile = firstPath.string();
    cupuacu::file::loadSampleData(&state);
    session.document.updateWaveformCache();
    REQUIRE(session.document.getWaveformCache(0).levelsCount() > 0);

    session.selection.setHighest(1'000'000.0);
    session.selection.setValue1(10.0);
    session.selection.setValue2(90.0);
    session.cursor = 123;
    REQUIRE(session.selection.isActive());
    REQUIRE(session.cursor == 123);

    session.currentFile = secondPath.string();
    cupuacu::file::loadSampleData(&state);

    REQUIRE(session.document.getSampleRate() == 22050);
    REQUIRE(session.document.getChannelCount() == 1);
    REQUIRE(session.document.getFrameCount() == 32);
    REQUIRE(session.document.getWaveformCache(0).levelsCount() == 0);
    REQUIRE_FALSE(session.selection.isActive());
    REQUIRE(session.cursor == 0);

    REQUIRE(session.document.getSample(0, 0) == Catch::Approx(0.0f));
    REQUIRE(session.document.getSample(0, 31) == Catch::Approx(31.0f / 64.0f));
}

TEST_CASE("Loading PCM16 and FLOAT64 files maps sample formats correctly",
          "[session]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-session-init-c"));

    const auto pcm16Path = cleanup.path() / "pcm16.wav";
    const auto float64Path = cleanup.path() / "float64.wav";

    writeTestWavWithFormat(pcm16Path, 32000, 1, SF_FORMAT_WAV | SF_FORMAT_PCM_16,
                           {0.25, -0.25, 0.5, -0.5});
    writeTestWavWithFormat(float64Path, 96000, 2, SF_FORMAT_WAV | SF_FORMAT_DOUBLE,
                           {0.1, -0.1, 0.2, -0.2, 0.3, -0.3});

    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();

    session.currentFile = pcm16Path.string();
    cupuacu::file::loadSampleData(&state);
    REQUIRE(session.document.getSampleFormat() ==
            cupuacu::SampleFormat::PCM_S16);
    REQUIRE(session.document.getSampleRate() == 32000);
    REQUIRE(session.document.getChannelCount() == 1);
    REQUIRE(session.document.getFrameCount() == 4);

    session.currentFile = float64Path.string();
    cupuacu::file::loadSampleData(&state);
    REQUIRE(session.document.getSampleFormat() ==
            cupuacu::SampleFormat::FLOAT64);
    REQUIRE(session.document.getSampleRate() == 96000);
    REQUIRE(session.document.getChannelCount() == 2);
    REQUIRE(session.document.getFrameCount() == 3);
}

TEST_CASE("Loading a missing file throws a descriptive error", "[session]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile =
        (makeUniqueTempDir("cupuacu-test-session-missing") / "missing.wav")
            .string();

    REQUIRE_THROWS_WITH(cupuacu::file::loadSampleData(&state),
                        Catch::Matchers::StartsWith("Failed to open file: "));
}

TEST_CASE("Opening a file failure preserves the existing session", "[session]")
{
    cupuacu::test::ScopedSdlLogSilencer silenceLogs;
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-open-preserve"));
    const auto goodPath = cleanup.path() / "good.wav";
    const auto unreadablePath = cleanup.path() / "not-audio";

    writeTestWav(goodPath, 44100, 1, {0.1f, 0.2f, 0.3f});
    std::filesystem::create_directories(unreadablePath);

    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();
    session.currentFile = goodPath.string();
    cupuacu::file::loadSampleData(&state);
    session.selection.setValue1(1.0);
    session.selection.setValue2(2.0);
    session.cursor = 1;

    std::string reportedTitle;
    std::string reportedMessage;
    state.errorReporter =
        [&](const std::string &title, const std::string &message)
    {
        reportedTitle = title;
        reportedMessage = message;
    };

    REQUIRE_FALSE(cupuacu::actions::loadFileIntoSession(
        &state, unreadablePath.string(), true, true, false));
    REQUIRE(reportedTitle == "Open failed");
    REQUIRE(reportedMessage.find(unreadablePath.string()) != std::string::npos);
    REQUIRE(session.currentFile == goodPath.string());
    REQUIRE(session.document.getSampleRate() == 44100);
    REQUIRE(session.document.getChannelCount() == 1);
    REQUIRE(session.document.getFrameCount() == 3);
}

TEST_CASE("Startup restore skips unreadable existing paths", "[session]")
{
    cupuacu::test::ScopedSdlLogSilencer silenceLogs;
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-startup-restore-unreadable"));
    const auto validPath = cleanup.path() / "valid.wav";
    const auto unreadablePath = cleanup.path() / "not-audio";
    writeTestWav(validPath, 48000, 2, {0.1f, -0.1f, 0.2f, -0.2f});
    std::filesystem::create_directories(unreadablePath);

    cupuacu::test::StateWithTestPaths state{};
    cupuacu::persistence::PersistedSessionState persistedState{};
    persistedState.openFiles = {unreadablePath.string(), validPath.string()};
    persistedState.activeOpenFileIndex = 1;

    std::string reportedTitle;
    std::string reportedMessage;
    state.errorReporter =
        [&](const std::string &title, const std::string &message)
    {
        reportedTitle = title;
        reportedMessage = message;
    };

    cupuacu::actions::restoreStartupDocument(
        &state, {unreadablePath.string(), validPath.string()}, persistedState);

    REQUIRE(state.tabs.size() == 1);
    REQUIRE(state.getActiveDocumentSession().currentFile == validPath.string());
    REQUIRE(state.getActiveDocumentSession().document.getSampleRate() == 48000);
    REQUIRE(state.recentFiles == std::vector<std::string>{validPath.string()});
    REQUIRE(reportedTitle == "Some files could not be reopened");
    REQUIRE(reportedMessage.find(unreadablePath.string()) != std::string::npos);
}

TEST_CASE("Startup restore reapplies persisted zoom offset and selection",
          "[session]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-startup-restore-view"));
    const auto validPath = cleanup.path() / "view.wav";
    writeTestWav(validPath, 32000, 1, {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f});
    cupuacu::file::wav::markers::rewriteFileWithMarkers(
        validPath, {{.id = 1, .frame = 0, .label = "Native"}} );

    cupuacu::test::StateWithTestPaths state{};
    cupuacu::persistence::PersistedSessionState persistedState{};
    persistedState.openDocuments = {
        {
            .filePath = validPath.string(),
            .samplesPerPixel = 2.75,
            .sampleOffset = 2,
            .selectionStart = 1,
            .selectionEndExclusive = 5,
            .markers = {
                {.id = 4, .frame = 2, .label = "Transient"},
                {.id = 7, .frame = 99, .label = "Tail"},
            },
        },
    };
    persistedState.openFiles = {validPath.string()};
    persistedState.activeOpenFileIndex = 0;

    cupuacu::actions::restoreStartupDocument(
        &state, {validPath.string()}, persistedState);

    const auto &session = state.getActiveDocumentSession();
    const auto &viewState = state.getActiveViewState();
    REQUIRE(session.currentFile == validPath.string());
    REQUIRE(viewState.samplesPerPixel == Catch::Approx(2.75));
    REQUIRE(viewState.sampleOffset == 2);
    REQUIRE(session.selection.isActive());
    REQUIRE(session.selection.getStartInt() == 1);
    REQUIRE(session.selection.getEndExclusiveInt() == 5);
    REQUIRE(session.document.getMarkers().size() == 2);
    REQUIRE(session.document.getMarkers()[0].id == 4);
    REQUIRE(session.document.getMarkers()[0].frame == 2);
    REQUIRE(session.document.getMarkers()[0].label == "Transient");
    REQUIRE(session.document.getMarkers()[1].id == 7);
    REQUIRE(session.document.getMarkers()[1].frame == 6);
    REQUIRE(session.document.getMarkers()[1].label == "Tail");
}
