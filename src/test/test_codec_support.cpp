#include <catch2/catch_test_macros.hpp>

#include "file/SndfilePath.hpp"
#include "file/file_loading.hpp"

#include <sndfile.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    class TemporaryDirectory
    {
    public:
        TemporaryDirectory()
        {
            static std::atomic_uint64_t sequence{0};
            const auto timestamp =
                std::chrono::steady_clock::now().time_since_epoch().count();
            root = std::filesystem::temp_directory_path() /
                   ("cupuacu-codec-test-" + std::to_string(timestamp) + "-" +
                    std::to_string(sequence.fetch_add(1)));
            std::filesystem::create_directories(root);
        }

        ~TemporaryDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }

        const std::filesystem::path &path() const
        {
            return root;
        }

    private:
        std::filesystem::path root;
    };

    std::vector<float> makeTestSignal(const sf_count_t frames,
                                      const int channels)
    {
        constexpr double pi = 3.14159265358979323846;
        std::vector<float> samples(static_cast<std::size_t>(frames) *
                                   static_cast<std::size_t>(channels));
        for (sf_count_t frame = 0; frame < frames; ++frame)
        {
            const auto phase =
                2.0 * pi * 440.0 * static_cast<double>(frame) / 44100.0;
            for (int channel = 0; channel < channels; ++channel)
            {
                samples[static_cast<std::size_t>(frame) * channels + channel] =
                    static_cast<float>(0.5 * std::sin(phase + channel * 0.2));
            }
        }
        return samples;
    }

    void writeAudioFile(const std::filesystem::path &path, const int format,
                        const sf_count_t frames = 4096)
    {
        constexpr int channels = 2;
        SF_INFO info{};
        info.channels = channels;
        info.samplerate = 44100;
        info.format = format;

        SNDFILE *file = cupuacu::file::openSndfile(path, SFM_WRITE, &info);
        REQUIRE(file != nullptr);
        const auto samples = makeTestSignal(frames, channels);
        REQUIRE(sf_writef_float(file, samples.data(), frames) == frames);
        REQUIRE(sf_close(file) == 0);
    }

    void requireReadableAudioFile(const std::filesystem::path &path)
    {
        SF_INFO info{};
        SNDFILE *file = cupuacu::file::openSndfile(path, SFM_READ, &info);
        REQUIRE(file != nullptr);
        REQUIRE(info.channels == 2);
        REQUIRE(info.samplerate == 44100);

        std::vector<float> decoded(static_cast<std::size_t>(info.channels) *
                                   4096u);
        const auto framesRead = sf_readf_float(file, decoded.data(), 4096);
        REQUIRE(framesRead > 0);
        REQUIRE(std::any_of(decoded.begin(),
                            decoded.begin() + framesRead * info.channels,
                            [](const float sample)
                            {
                                return std::abs(sample) > 0.001f;
                            }));
        REQUIRE(sf_close(file) == 0);
    }

    void addId3v23Prefix(const std::filesystem::path &source,
                         const std::filesystem::path &destination)
    {
        std::ifstream input(source, std::ios::binary);
        REQUIRE(input.good());
        const std::vector<char> flacBytes{std::istreambuf_iterator<char>(input),
                                          std::istreambuf_iterator<char>()};
        REQUIRE_FALSE(flacBytes.empty());

        // Match the 4,705-byte ID3v2.3 prefix (header plus payload) seen on
        // real-world FLAC collections that store leading artwork and metadata.
        constexpr std::array<std::uint8_t, 10> id3Header{
            'I', 'D', '3', 3, 0, 0, 0, 0, 0x24, 0x57,
        };
        std::ofstream output(destination, std::ios::binary);
        REQUIRE(output.good());
        output.write(reinterpret_cast<const char *>(id3Header.data()),
                     static_cast<std::streamsize>(id3Header.size()));
        constexpr std::array<char, 4695> padding{};
        output.write(padding.data(),
                     static_cast<std::streamsize>(padding.size()));
        output.write(flacBytes.data(),
                     static_cast<std::streamsize>(flacBytes.size()));
        REQUIRE(output.good());
    }
} // namespace

TEST_CASE("Required packaged audio codecs are enabled", "[codec]")
{
    constexpr std::array formats{
        SF_FORMAT_FLAC | SF_FORMAT_PCM_16,
        SF_FORMAT_FLAC | SF_FORMAT_PCM_24,
        SF_FORMAT_OGG | SF_FORMAT_VORBIS,
        SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III,
    };

    for (const auto format : formats)
    {
        SF_INFO info{};
        info.channels = 2;
        info.samplerate = 44100;
        info.format = format;
        REQUIRE(sf_format_check(&info) != 0);
    }
}

TEST_CASE("Packaged codecs write and read FLAC Vorbis and MP3", "[codec]")
{
    TemporaryDirectory temporaryDirectory;
    const std::array cases{
        std::pair{"round-trip.flac", SF_FORMAT_FLAC | SF_FORMAT_PCM_24},
        std::pair{"round-trip.ogg", SF_FORMAT_OGG | SF_FORMAT_VORBIS},
        std::pair{"round-trip.mp3", SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III},
    };

    for (const auto &[filename, format] : cases)
    {
        const auto path = temporaryDirectory.path() / filename;
        writeAudioFile(path, format);
        requireReadableAudioFile(path);
    }
}

TEST_CASE("Cupuacu opens an ID3v2-prefixed FLAC file", "[codec]")
{
    TemporaryDirectory temporaryDirectory;
    const auto plainPath = temporaryDirectory.path() / "plain.flac";
    const auto taggedPath = temporaryDirectory.path() / "id3-prefixed.flac";
    writeAudioFile(plainPath, SF_FORMAT_FLAC | SF_FORMAT_PCM_16, 1024);
    addId3v23Prefix(plainPath, taggedPath);

    const auto loaded = cupuacu::file::loadAudioFile(taggedPath.string());
    REQUIRE(loaded.document.getSampleRate() == 44100);
    REQUIRE(loaded.document.getChannelCount() == 2);
    REQUIRE(loaded.document.getFrameCount() == 1024);
}
