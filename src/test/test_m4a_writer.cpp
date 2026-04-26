#include <catch2/catch_test_macros.hpp>

#include "Document.hpp"
#include "SampleFormat.hpp"
#include "State.hpp"
#include "file/AudioExport.hpp"
#include "file/AudioFileWriter.hpp"
#include "file/file_loading.hpp"
#include "file/m4a/M4aAlacWriter.hpp"
#include "file/m4a/M4aAlacReader.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    class ScopedDirCleanup
    {
    public:
        explicit ScopedDirCleanup(std::filesystem::path pathToRemove)
            : path(std::move(pathToRemove))
        {
        }

        ~ScopedDirCleanup()
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }

        [[nodiscard]] const std::filesystem::path &get() const
        {
            return path;
        }

    private:
        std::filesystem::path path;
    };

    std::filesystem::path makeUniqueTempDir(const std::string &prefix)
    {
        const auto base = std::filesystem::temp_directory_path();
        const auto tick = static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        for (int attempt = 0; attempt < 64; ++attempt)
        {
            auto candidate =
                base / (prefix + "-" + std::to_string(tick) + "-" +
                        std::to_string(attempt));
            std::error_code ec;
            if (std::filesystem::create_directory(candidate, ec))
            {
                return candidate;
            }
        }
        throw std::runtime_error("Failed to create temporary test directory");
    }

    std::vector<std::uint8_t> readBytes(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        REQUIRE(input);
        return std::vector<std::uint8_t>(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
    }

    std::string asciiAt(const std::vector<std::uint8_t> &bytes,
                        const std::size_t offset)
    {
        return std::string(reinterpret_cast<const char *>(bytes.data() + offset),
                           4);
    }

    std::uint32_t readBe32(const std::vector<std::uint8_t> &bytes,
                           const std::size_t offset)
    {
        return (static_cast<std::uint32_t>(bytes[offset]) << 24u) |
               (static_cast<std::uint32_t>(bytes[offset + 1]) << 16u) |
               (static_cast<std::uint32_t>(bytes[offset + 2]) << 8u) |
               static_cast<std::uint32_t>(bytes[offset + 3]);
    }

    std::size_t findChildOffset(const std::vector<std::uint8_t> &bytes,
                                const std::size_t childrenStart,
                                const std::string &type)
    {
        for (std::size_t offset = childrenStart; offset + 8 <= bytes.size();)
        {
            const auto size = readBe32(bytes, offset);
            if (size < 8 || offset + size > bytes.size())
            {
                throw std::runtime_error("Invalid child atom size");
            }
            if (asciiAt(bytes, offset + 4) == type)
            {
                return offset;
            }
            offset += size;
        }
        throw std::runtime_error("Child atom not found");
    }

    cupuacu::Document makeStereoDocument()
    {
        cupuacu::Document document;
        document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 4);
        document.setSample(0, 0, 0.0f, false);
        document.setSample(1, 0, -0.1f, false);
        document.setSample(0, 1, 0.25f, false);
        document.setSample(1, 1, -0.25f, false);
        document.setSample(0, 2, 0.5f, false);
        document.setSample(1, 2, -0.5f, false);
        document.setSample(0, 3, 0.75f, false);
        document.setSample(1, 3, -0.75f, false);
        return document;
    }
} // namespace

TEST_CASE("M4A ALAC writer writes ftyp mdat moov file", "[m4a]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-m4a-writer"));
    const auto outputPath = cleanup.get() / "test.m4a";

    cupuacu::file::m4a::writeAlacM4aFile(makeStereoDocument(), outputPath);

    const auto bytes = readBytes(outputPath);
    const auto ftypSize = readBe32(bytes, 0);
    const auto mdatOffset = static_cast<std::size_t>(ftypSize);
    const auto mdatSize = readBe32(bytes, mdatOffset);
    const auto moovOffset = mdatOffset + mdatSize;

    REQUIRE(asciiAt(bytes, 4) == "ftyp");
    REQUIRE(asciiAt(bytes, mdatOffset + 4) == "mdat");
    REQUIRE(asciiAt(bytes, moovOffset + 4) == "moov");
    REQUIRE(mdatSize > 8);

    const auto trakOffset = findChildOffset(bytes, moovOffset + 8, "trak");
    const auto mdiaOffset = findChildOffset(bytes, trakOffset + 8, "mdia");
    const auto minfOffset = findChildOffset(bytes, mdiaOffset + 8, "minf");
    const auto stblOffset = findChildOffset(bytes, minfOffset + 8, "stbl");
    const auto stcoOffset = findChildOffset(bytes, stblOffset + 8, "stco");

    REQUIRE(readBe32(bytes, stcoOffset + 12) == 1);
    REQUIRE(readBe32(bytes, stcoOffset + 16) == ftypSize + 8);
}

TEST_CASE("M4A ALAC writer rejects invalid documents", "[m4a]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-m4a-writer-invalid"));
    cupuacu::Document document;
    document.initialize(cupuacu::SampleFormat::FLOAT32, 0, 2, 4);

    REQUIRE_THROWS_AS(cupuacu::file::m4a::writeAlacM4aFile(
                          document, cleanup.get() / "invalid.m4a"),
                      std::invalid_argument);
}

TEST_CASE("M4A ALAC export settings are available by default", "[m4a]")
{
    const auto settings = cupuacu::file::defaultExportSettingsForPath(
        "song.m4a", cupuacu::SampleFormat::FLOAT32);

    REQUIRE(settings.has_value());
    REQUIRE(settings->container == cupuacu::file::AudioExportContainer::M4A);
    REQUIRE(settings->codec == cupuacu::file::AudioExportCodec::ALAC);
    REQUIRE(settings->majorFormat == cupuacu::file::CUPUACU_FORMAT_M4A);
    REQUIRE(settings->subtype == cupuacu::file::CUPUACU_FORMAT_ALAC);
    REQUIRE(settings->extension == "m4a");
    REQUIRE(cupuacu::file::isNativeM4aAlacExportSettings(*settings));
}

TEST_CASE("M4A ALAC export format is probed without libsndfile support",
          "[m4a]")
{
    const auto formats = cupuacu::file::probeAvailableExportFormats();
    const auto it = std::find_if(
        formats.begin(), formats.end(),
        [](const cupuacu::file::AudioExportFormatOption &format)
        {
            return format.container ==
                       cupuacu::file::AudioExportContainer::M4A &&
                   format.codec == cupuacu::file::AudioExportCodec::ALAC;
        });

    REQUIRE(it != formats.end());
    REQUIRE(it->majorFormat == cupuacu::file::CUPUACU_FORMAT_M4A);
    REQUIRE(it->encodings.size() == 1);
    REQUIRE(it->encodings.front().subtype ==
            cupuacu::file::CUPUACU_FORMAT_ALAC);
    REQUIRE(it->encodings.front().extension == "m4a");
}

TEST_CASE("M4A ALAC open format is offered by the file dialog filters", "[m4a]")
{
    const auto formats = cupuacu::file::probeAvailableOpenFormats();
    const auto it = std::find_if(
        formats.begin(), formats.end(),
        [](const cupuacu::file::AudioOpenFormatOption &format)
        {
            return format.majorFormat == cupuacu::file::CUPUACU_FORMAT_M4A;
        });

    REQUIRE(it != formats.end());
    REQUIRE(it->extensions.size() == 2);
    REQUIRE(it->extensions[0] == "m4a");
    REQUIRE(it->extensions[1] == "mp4");
}

TEST_CASE("AudioFileWriter routes M4A ALAC exports to native writer", "[m4a]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-m4a-audio-file-writer"));
    const auto outputPath = cleanup.get() / "public-writer.m4a";
    auto settings = cupuacu::file::defaultExportSettingsForPath(
        outputPath, cupuacu::SampleFormat::FLOAT32);
    REQUIRE(settings.has_value());

    cupuacu::State state;
    state.getActiveDocumentSession().document = makeStereoDocument();

    cupuacu::file::AudioFileWriter::writeFile(&state, outputPath, *settings);

    const auto bytes = readBytes(outputPath);
    const auto ftypSize = readBe32(bytes, 0);
    const auto mdatOffset = static_cast<std::size_t>(ftypSize);
    const auto mdatSize = readBe32(bytes, mdatOffset);
    const auto moovOffset = mdatOffset + mdatSize;

    REQUIRE(asciiAt(bytes, 4) == "ftyp");
    REQUIRE(asciiAt(bytes, mdatOffset + 4) == "mdat");
    REQUIRE(asciiAt(bytes, moovOffset + 4) == "moov");
}

TEST_CASE("M4A ALAC files are readable by the native loader", "[m4a]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-m4a-reader"));
    const auto outputPath = cleanup.get() / "readable.m4a";

    cupuacu::file::m4a::writeAlacM4aFile(makeStereoDocument(), outputPath);

    const auto audio = cupuacu::file::m4a::readAlacM4aFile(outputPath);
    REQUIRE(audio.sampleRate == 44100);
    REQUIRE(audio.channels == 2);
    REQUIRE(audio.frameCount == 4);
    REQUIRE(audio.interleavedPcm16Samples.size() == 8);

    cupuacu::State state;
    state.getActiveDocumentSession().currentFile = outputPath.string();
    cupuacu::file::loadSampleData(&state);

    const auto &session = state.getActiveDocumentSession();
    REQUIRE(session.document.getSampleFormat() == cupuacu::SampleFormat::PCM_S16);
    REQUIRE(session.document.getSampleRate() == 44100);
    REQUIRE(session.document.getChannelCount() == 2);
    REQUIRE(session.document.getFrameCount() == 4);
    REQUIRE(session.currentFileExportSettings.has_value());
    REQUIRE(session.currentFileExportSettings->container ==
            cupuacu::file::AudioExportContainer::M4A);
    REQUIRE(std::fabs(session.document.getSample(0, 3) - 0.75f) < 0.001f);
    REQUIRE(std::fabs(session.document.getSample(1, 3) + 0.75f) < 0.001f);
}
