#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestPaths.hpp"
#include "actions/Save.hpp"
#include "file/AudioExport.hpp"
#include "file/SndfilePath.hpp"
#include "file/file_loading.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/Window.hpp"

#include <sndfile.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
#include <utility>
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
            const auto tick = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(now)
                    .count());
            const auto candidate =
                tempRoot / (prefix + "-" + std::to_string(tick) + "-" +
                            std::to_string(dis(gen)));
            std::error_code ec;
            if (!std::filesystem::exists(candidate, ec))
            {
                return candidate;
            }
        }

        return tempRoot / (prefix + "-fallback");
    }

    void appendByte(std::vector<uint8_t> &bytes, const uint8_t value)
    {
        bytes.push_back(value);
    }

    void appendLe16(std::vector<uint8_t> &bytes, const uint16_t value)
    {
        appendByte(bytes, static_cast<uint8_t>(value & 0xffu));
        appendByte(bytes, static_cast<uint8_t>((value >> 8) & 0xffu));
    }

    void appendLe32(std::vector<uint8_t> &bytes, const uint32_t value)
    {
        appendByte(bytes, static_cast<uint8_t>(value & 0xffu));
        appendByte(bytes, static_cast<uint8_t>((value >> 8) & 0xffu));
        appendByte(bytes, static_cast<uint8_t>((value >> 16) & 0xffu));
        appendByte(bytes, static_cast<uint8_t>((value >> 24) & 0xffu));
    }

    void appendAscii(std::vector<uint8_t> &bytes, const char *text)
    {
        while (*text != '\0')
        {
            appendByte(bytes, static_cast<uint8_t>(*text));
            ++text;
        }
    }

    void appendChunk(std::vector<uint8_t> &bytes, const char *chunkId,
                     const std::vector<uint8_t> &payload)
    {
        appendAscii(bytes, chunkId);
        appendLe32(bytes, static_cast<uint32_t>(payload.size()));
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        if ((payload.size() & 1u) != 0u)
        {
            appendByte(bytes, 0);
        }
    }

    void writePcm16WavFile(const std::filesystem::path &path,
                           const int sampleRate, const int channels,
                           const std::vector<int16_t> &interleavedSamples,
                           const std::vector<uint8_t> &preDataChunk = {},
                           const std::vector<uint8_t> &postDataChunk = {})
    {
        REQUIRE(channels > 0);
        REQUIRE(interleavedSamples.size() % static_cast<size_t>(channels) == 0);

        std::vector<uint8_t> wavBytes;
        appendAscii(wavBytes, "RIFF");
        appendLe32(wavBytes, 0);
        appendAscii(wavBytes, "WAVE");

        std::vector<uint8_t> fmtChunk;
        appendLe16(fmtChunk, 1);
        appendLe16(fmtChunk, static_cast<uint16_t>(channels));
        appendLe32(fmtChunk, static_cast<uint32_t>(sampleRate));
        const uint32_t byteRate =
            static_cast<uint32_t>(sampleRate * channels * sizeof(int16_t));
        appendLe32(fmtChunk, byteRate);
        appendLe16(fmtChunk, static_cast<uint16_t>(channels * sizeof(int16_t)));
        appendLe16(fmtChunk, 16);
        appendChunk(wavBytes, "fmt ", fmtChunk);

        if (!preDataChunk.empty())
        {
            appendChunk(wavBytes, "JUNK", preDataChunk);
        }

        std::vector<uint8_t> dataChunk;
        dataChunk.reserve(interleavedSamples.size() * sizeof(int16_t));
        for (const int16_t sample : interleavedSamples)
        {
            appendLe16(dataChunk, static_cast<uint16_t>(sample));
        }
        appendChunk(wavBytes, "data", dataChunk);

        if (!postDataChunk.empty())
        {
            appendChunk(wavBytes, "LIST", postDataChunk);
        }

        const uint32_t riffSize = static_cast<uint32_t>(wavBytes.size() - 8);
        wavBytes[4] = static_cast<uint8_t>(riffSize & 0xffu);
        wavBytes[5] = static_cast<uint8_t>((riffSize >> 8) & 0xffu);
        wavBytes[6] = static_cast<uint8_t>((riffSize >> 16) & 0xffu);
        wavBytes[7] = static_cast<uint8_t>((riffSize >> 24) & 0xffu);

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write(reinterpret_cast<const char *>(wavBytes.data()),
                  static_cast<std::streamsize>(wavBytes.size()));
        REQUIRE(out.good());
    }

    std::vector<uint8_t> readBytes(const std::filesystem::path &path)
    {
        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.good());
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
    }

    std::vector<float> readFramesAsFloat(const std::filesystem::path &path,
                                         int &sampleRate, int &channels)
    {
        std::fprintf(stderr,
                     "CUPUACU_DEBUG_WAV_TEST: readFramesAsFloat path=%s\n",
                     path.string().c_str());
        std::fflush(stderr);
        SF_INFO info{};
        SNDFILE *file = cupuacu::file::openSndfile(path, SFM_READ, &info);
        std::fprintf(stderr,
                     "CUPUACU_DEBUG_WAV_TEST: readFramesAsFloat after open file=%p samplerate=%d channels=%d frames=%lld format=0x%x\n",
                     static_cast<void *>(file), info.samplerate, info.channels,
                     static_cast<long long>(info.frames), info.format);
        std::fflush(stderr);
        REQUIRE(file != nullptr);

        sampleRate = info.samplerate;
        channels = info.channels;
        std::vector<float> frames(static_cast<size_t>(info.frames * info.channels));
        const sf_count_t readCount =
            sf_readf_float(file, frames.data(), info.frames);
        sf_close(file);
        std::fprintf(stderr,
                     "CUPUACU_DEBUG_WAV_TEST: readFramesAsFloat readCount=%lld vectorSize=%zu\n",
                     static_cast<long long>(readCount), frames.size());
        std::fflush(stderr);
        REQUIRE(readCount == info.frames);
        return frames;
    }
} // namespace

TEST_CASE("Overwrite keeps untouched 16-bit PCM WAV byte-identical", "[file]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-wav-overwrite"));
    const auto wavPath = cleanup.path() / "untouched.wav";

    writePcm16WavFile(wavPath, 44100, 2,
                      {0, 1200, -1200, 32000, -32000, 42, -42, 8192});
    const auto originalBytes = readBytes(wavPath);

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);
    cupuacu::actions::overwrite(&state);

    REQUIRE(readBytes(wavPath) == originalBytes);
}

TEST_CASE("Overwrite preserves non-audio WAV chunks around data", "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-extra-chunks"));
    const auto wavPath = cleanup.path() / "with_chunks.wav";

    writePcm16WavFile(wavPath, 48000, 1, {100, -100, 200, -200},
                      {'p', 'r', 'e', '!', 1, 2},
                      {'p', 'o', 's', 't', 9, 8, 7, 6});
    const auto originalBytes = readBytes(wavPath);

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);
    cupuacu::actions::overwrite(&state);

    REQUIRE(readBytes(wavPath) == originalBytes);
}

TEST_CASE("Overwrite clips edited samples into valid PCM16 range", "[file]")
{
    std::fprintf(stderr,
                 "CUPUACU_DEBUG_WAV_TEST: begin case=overwrite_clips\n");
    std::fflush(stderr);
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-wav-clipping"));
    const auto wavPath = cleanup.path() / "clipped.wav";

    writePcm16WavFile(wavPath, 22050, 1, {0, 0, 0});

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);

    auto &document = state.getActiveDocumentSession().document;
    document.setSample(0, 0, 1.25f);
    document.setSample(0, 1, -1.25f);
    document.setSample(0, 2, 0.5f);

    std::fprintf(stderr,
                 "CUPUACU_DEBUG_WAV_TEST: before overwrite currentFile=%s\n",
                 state.getActiveDocumentSession().currentFile.c_str());
    std::fflush(stderr);
    cupuacu::actions::overwrite(&state);
    std::fprintf(stderr,
                 "CUPUACU_DEBUG_WAV_TEST: after overwrite exists=%d\n",
                 std::filesystem::exists(wavPath) ? 1 : 0);
    std::fflush(stderr);

    int sampleRate = 0;
    int channels = 0;
    const auto frames = readFramesAsFloat(wavPath, sampleRate, channels);
    std::fprintf(stderr,
                 "CUPUACU_DEBUG_WAV_TEST: after read framesSize=%zu sampleRate=%d channels=%d\n",
                 frames.size(), sampleRate, channels);
    std::fflush(stderr);
    REQUIRE(sampleRate == 22050);
    REQUIRE(channels == 1);
    REQUIRE(frames.size() == 3);
    REQUIRE(frames[0] == Catch::Approx(1.0f).margin(1.0f / 32767.0f));
    REQUIRE(frames[1] == Catch::Approx(-1.0f).margin(1.0f / 32767.0f));
    REQUIRE(frames[2] == Catch::Approx(0.5f).margin(1.0f / 32767.0f));
}

TEST_CASE("Save as writes a new WAV file and updates active file state", "[file]")
{
    std::fprintf(stderr,
                 "CUPUACU_DEBUG_WAV_TEST: begin case=save_as_wav\n");
    std::fflush(stderr);
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-save-as"));
    const auto outputPath = cleanup.path() / "exports" / "saved.wav";

    cupuacu::test::StateWithTestPaths state{cleanup.path()};

    auto &session = state.getActiveDocumentSession();
    session.document.initialize(cupuacu::SampleFormat::PCM_S16, 22050, 2, 3);
    session.document.setSample(0, 0, -1.0f, false);
    session.document.setSample(1, 0, 1.0f, false);
    session.document.setSample(0, 1, 0.5f, false);
    session.document.setSample(1, 1, -0.5f, false);
    session.document.setSample(0, 2, 0.0f, false);
    session.document.setSample(1, 2, 0.25f, false);

    std::fprintf(stderr,
                 "CUPUACU_DEBUG_WAV_TEST: before saveAs output=%s\n",
                 outputPath.string().c_str());
    std::fflush(stderr);
    cupuacu::actions::saveAs(&state, outputPath.string());
    std::fprintf(stderr,
                 "CUPUACU_DEBUG_WAV_TEST: after saveAs exists=%d currentFile=%s recentFiles=%zu\n",
                 std::filesystem::exists(outputPath) ? 1 : 0,
                 session.currentFile.c_str(), state.recentFiles.size());
    std::fflush(stderr);

    REQUIRE(std::filesystem::exists(outputPath));
    REQUIRE(session.currentFile == outputPath.string());
    REQUIRE(session.currentFileExportSettings.has_value());
    REQUIRE(session.currentFileExportSettings->majorFormat == SF_FORMAT_WAV);
    REQUIRE(session.currentFileExportSettings->subtype == SF_FORMAT_PCM_16);
    REQUIRE(state.recentFiles.size() == 1);
    REQUIRE(state.recentFiles.front() == outputPath.string());

    int sampleRate = 0;
    int channels = 0;
    const auto frames = readFramesAsFloat(outputPath, sampleRate, channels);
    REQUIRE(sampleRate == 22050);
    REQUIRE(channels == 2);
    REQUIRE(frames.size() == 6);
    REQUIRE(frames[0] == Catch::Approx(-1.0f).margin(1.0f / 32767.0f));
    REQUIRE(frames[1] == Catch::Approx(1.0f).margin(1.0f / 32767.0f));
    REQUIRE(frames[2] == Catch::Approx(0.5f).margin(1.0f / 32767.0f));
    REQUIRE(frames[3] == Catch::Approx(-0.5f).margin(1.0f / 32767.0f));
    REQUIRE(frames[4] == Catch::Approx(0.0f).margin(1.0f / 32767.0f));
    REQUIRE(frames[5] == Catch::Approx(0.25f).margin(1.0f / 32767.0f));
}

TEST_CASE("Save as normalizes the output extension to the selected format",
          "[file]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-save-as-extension"));
    const auto requestedPath = cleanup.path() / "exports" / "saved.flac";
    const auto normalizedPath = cleanup.path() / "exports" / "saved.wav";

    cupuacu::test::StateWithTestPaths state{cleanup.path()};

    auto &session = state.getActiveDocumentSession();
    session.document.initialize(cupuacu::SampleFormat::PCM_S24, 44100, 1, 2);
    session.document.setSample(0, 0, 0.25f, false);
    session.document.setSample(0, 1, -0.25f, false);

    cupuacu::file::AudioExportSettings settings{
        .container = cupuacu::file::AudioExportContainer::WAV,
        .codec = cupuacu::file::AudioExportCodec::PCM,
        .majorFormat = SF_FORMAT_WAV,
        .subtype = SF_FORMAT_PCM_24,
        .containerLabel = "WAV",
        .codecLabel = "PCM",
        .encodingLabel = "24-bit PCM",
        .extension = "wav",
    };

    cupuacu::actions::saveAs(&state, requestedPath.string(), settings);

    REQUIRE_FALSE(std::filesystem::exists(requestedPath));
    REQUIRE(std::filesystem::exists(normalizedPath));
    REQUIRE(session.currentFile == normalizedPath.string());
    REQUIRE(session.currentFileExportSettings.has_value());
    REQUIRE(session.currentFileExportSettings->majorFormat == SF_FORMAT_WAV);
}

TEST_CASE("Default export settings preserve ALAC depth preferences", "[file]")
{
    const auto settings = cupuacu::file::defaultExportSettingsForPath(
        "export.caf", cupuacu::SampleFormat::PCM_S24);

    if (!settings.has_value())
    {
        SUCCEED("ALAC export is not available in this libsndfile build.");
        return;
    }
    REQUIRE(settings->majorFormat == SF_FORMAT_CAF);
    REQUIRE(settings->subtype == SF_FORMAT_ALAC_24);
}

TEST_CASE("Open format probe includes WAV support", "[file]")
{
    const auto formats = cupuacu::file::probeAvailableOpenFormats();

    const auto it = std::find_if(
        formats.begin(), formats.end(),
        [](const cupuacu::file::AudioOpenFormatOption &format)
        { return format.majorFormat == SF_FORMAT_WAV; });
    REQUIRE(it != formats.end());
    REQUIRE(std::find(it->extensions.begin(), it->extensions.end(), "wav") !=
            it->extensions.end());
}

TEST_CASE("Import sample format inference handles compressed and ALAC input",
          "[file]")
{
    REQUIRE(cupuacu::file::sampleFormatForSndfileFormat(
                SF_FORMAT_WAV | SF_FORMAT_PCM_16) ==
            cupuacu::SampleFormat::PCM_S16);
    REQUIRE(cupuacu::file::sampleFormatForSndfileFormat(
                SF_FORMAT_CAF | SF_FORMAT_ALAC_24) ==
            cupuacu::SampleFormat::PCM_S24);
    REQUIRE(cupuacu::file::sampleFormatForSndfileFormat(
                SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III) ==
            cupuacu::SampleFormat::FLOAT32);
    REQUIRE(cupuacu::file::sampleFormatForSndfileFormat(
                SF_FORMAT_OGG | SF_FORMAT_VORBIS) ==
            cupuacu::SampleFormat::FLOAT32);
}

TEST_CASE("Lossy export defaults expose Vorbis quality and MP3 bitrate mode",
          "[file]")
{
    const auto vorbisQuality =
        cupuacu::file::defaultCompressionLevelForCodec(
            cupuacu::file::AudioExportCodec::VORBIS);
    REQUIRE(vorbisQuality.has_value());
    REQUIRE(*vorbisQuality == Catch::Approx(0.7));

    const auto mp3BitrateMode =
        cupuacu::file::defaultBitrateModeForCodec(
            cupuacu::file::AudioExportCodec::MP3);
    REQUIRE(mp3BitrateMode.has_value());
    REQUIRE(*mp3BitrateMode == SF_BITRATE_MODE_VARIABLE);

    const auto mp3Modes =
        cupuacu::file::bitrateModeOptionsForCodec(
            cupuacu::file::AudioExportCodec::MP3);
    REQUIRE(mp3Modes.size() == 3);
}

TEST_CASE("MP3 bitrate options follow sample rate band and mode", "[file]")
{
    cupuacu::file::AudioExportSettings cbrSettings{
        .container = cupuacu::file::AudioExportContainer::MPEG,
        .codec = cupuacu::file::AudioExportCodec::MP3,
        .majorFormat = SF_FORMAT_MPEG,
        .subtype = SF_FORMAT_MPEG_LAYER_III,
        .containerLabel = "MPEG",
        .codecLabel = "MP3",
        .encodingLabel = "Default quality",
        .extension = "mp3",
        .bitrateMode = SF_BITRATE_MODE_CONSTANT,
    };
    const auto mpeg1Options =
        cupuacu::file::bitrateOptionsForSettings(cbrSettings, 44100);
    REQUIRE_FALSE(mpeg1Options.empty());
    REQUIRE(mpeg1Options.front().value == 32);
    REQUIRE(mpeg1Options.back().value == 320);

    const auto mpeg25Options =
        cupuacu::file::bitrateOptionsForSettings(cbrSettings, 12000);
    REQUIRE_FALSE(mpeg25Options.empty());
    REQUIRE(mpeg25Options.front().value == 8);
    REQUIRE(mpeg25Options.back().value == 64);

    cbrSettings.bitrateMode = SF_BITRATE_MODE_VARIABLE;
    REQUIRE(cupuacu::file::bitrateOptionsForSettings(cbrSettings, 44100).empty());
}

TEST_CASE("Export settings description includes lossy quality controls", "[file]")
{
    cupuacu::file::AudioExportSettings settings{
        .container = cupuacu::file::AudioExportContainer::MPEG,
        .codec = cupuacu::file::AudioExportCodec::MP3,
        .majorFormat = SF_FORMAT_MPEG,
        .subtype = SF_FORMAT_MPEG_LAYER_III,
        .containerLabel = "MPEG",
        .codecLabel = "MP3",
        .encodingLabel = "Default quality",
        .extension = "mp3",
        .compressionLevel = 0.75,
        .bitrateMode = SF_BITRATE_MODE_VARIABLE,
    };

    const auto description = cupuacu::file::describeExportSettings(settings);
    REQUIRE(description.find("VBR") != std::string::npos);
    REQUIRE(description.find("quality 75%") != std::string::npos);
}

TEST_CASE("Export settings description includes MP3 bitrate when selected",
          "[file]")
{
    cupuacu::file::AudioExportSettings settings{
        .container = cupuacu::file::AudioExportContainer::MPEG,
        .codec = cupuacu::file::AudioExportCodec::MP3,
        .majorFormat = SF_FORMAT_MPEG,
        .subtype = SF_FORMAT_MPEG_LAYER_III,
        .containerLabel = "MPEG",
        .codecLabel = "MP3",
        .encodingLabel = "Default quality",
        .extension = "mp3",
        .bitrateMode = SF_BITRATE_MODE_CONSTANT,
        .bitrateKbps = 192,
    };

    const auto description = cupuacu::file::describeExportSettings(settings);
    REQUIRE(description.find("CBR") != std::string::npos);
    REQUIRE(description.find("192 kbps") != std::string::npos);
}
