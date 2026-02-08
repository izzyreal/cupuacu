#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "State.hpp"
#include "file/file_loading.hpp"
#include "gui/DevicePropertiesWindow.hpp"
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

TEST_CASE("Loading a file resets session selection and cursor", "[session]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-session-init-a"));

    const auto wavPath = cleanup.path() / "session_a.wav";
    const std::vector<float> samples = {
        0.1f, -0.1f, 0.2f, -0.2f, 0.3f, -0.3f, 0.4f, -0.4f};
    writeTestWav(wavPath, 48000, 2, samples);

    cupuacu::State state{};
    auto &session = state.activeDocumentSession;
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

    cupuacu::State state{};
    auto &session = state.activeDocumentSession;

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
