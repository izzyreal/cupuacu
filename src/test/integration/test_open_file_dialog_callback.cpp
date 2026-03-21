#include <catch2/catch_test_macros.hpp>

#include "IntegrationTestHelpers.hpp"

#include "State.hpp"
#include "actions/ShowOpenFileDialog.hpp"
#include "gui/DevicePropertiesWindow.hpp"

#include <sndfile.h>

#include <chrono>
#include <cstdint>
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

        SNDFILE *file = sf_open(path.string().c_str(), SFM_WRITE, &info);
        REQUIRE(file != nullptr);

        const sf_count_t frameCount =
            static_cast<sf_count_t>(interleavedFrames.size() / channels);
        const sf_count_t written =
            sf_writef_float(file, interleavedFrames.data(), frameCount);

        sf_close(file);
        REQUIRE(written == frameCount);
    }
} // namespace

TEST_CASE("Open file dialog callback loads the selected file into the session",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-open-dialog"));
    const auto wavPath = cleanup.path() / "selected.wav";
    writeTestWav(wavPath, 22050, 2,
                 {0.25f, -0.25f, 0.5f, -0.5f, 0.75f, -0.75f});

    cupuacu::State state{};
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

    REQUIRE(state.tabs.size() == 2);
    REQUIRE(state.activeTabIndex == 1);
    REQUIRE(state.tabs[0].session.currentFile == "before.wav");
    REQUIRE(state.tabs[0].viewState.verticalZoom == 4);
    REQUIRE(state.tabs[0].viewState.sampleOffset == 9);

    auto &session = state.getActiveDocumentSession();
    auto &viewState = state.getActiveViewState();
    REQUIRE(session.currentFile == pathString);
    REQUIRE(session.document.getSampleRate() == 22050);
    REQUIRE(session.document.getChannelCount() == 2);
    REQUIRE(session.document.getFrameCount() == 3);
    REQUIRE_FALSE(session.selection.isActive());
    REQUIRE(session.cursor == 0);
    REQUIRE(viewState.verticalZoom == 1);
    REQUIRE(viewState.sampleOffset == 0);
    REQUIRE(viewState.samplesToScroll == 0);
    REQUIRE(viewState.selectedChannels == cupuacu::SelectedChannels::BOTH);
    REQUIRE(state.waveforms.size() == 2);
    REQUIRE(std::string(SDL_GetWindowTitle(
                state.mainDocumentSessionWindow->getWindow()->getSdlWindow())) ==
            pathString);
}

TEST_CASE("Open file dialog callback leaves state unchanged when canceled",
          "[integration]")
{
    cupuacu::State state{};
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
    cupuacu::State state{};
    state.getActiveDocumentSession().currentFile = "still-unchanged.wav";

    REQUIRE_NOTHROW(cupuacu::actions::fileDialogCallback(&state, nullptr, 0));
    REQUIRE(state.getActiveDocumentSession().currentFile == "still-unchanged.wav");
}
