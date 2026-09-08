#include "file/m4a/M4aAlacReader.hpp"
#include <catch2/catch_test_macros.hpp>
#include "TestPaths.hpp"
#include "file/AudioFileWriter.hpp"
#include "file/FileIo.hpp"
#include "file/PcmPreservationIO.hpp"
#include "file/LegacyAudioLoading.hpp"
#include "file/m4a/M4aAtoms.hpp"
#include "file/m4a/M4aParser.hpp"
#include "LongTask.hpp"
#include "storage/AudioEditRevision.hpp"
#include <fstream>
#include <sstream>
#include <cstring>

using namespace cupuacu;
namespace
{
    struct ExportFiles
    {
        std::filesystem::path root =
            test::makeUniqueTestRoot("streaming-export");
        ExportFiles()
        {
            std::filesystem::create_directories(root);
        }
        ~ExportFiles()
        {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }
    };
    struct GeneratedReader : storage::AudioReader
    {
        storage::AudioShape dimensions{65536 + 17, 2, 48000,
                                       SampleFormat::PCM_S16};
        mutable std::size_t largestRead = 0;
        int64_t failAt = -1;
        storage::AudioShape shape() const override
        {
            return dimensions;
        }
        static float value(int64_t frame, int channel)
        {
            return float((frame * 31 + channel * 73) % 8192 - 4096) / 32768;
        }
        void readChannel(int channel, int64_t start,
                         std::span<float> output) const override
        {
            validateRange(shape(), channel, start, output.size());
            largestRead = std::max(largestRead, output.size());
            if (failAt >= 0 && start >= failAt)
            {
                throw std::runtime_error("injected read failure");
            }
            for (std::size_t i = 0; i < output.size(); ++i)
            {
                output[i] = value(start + i, channel);
            }
        }
    };
    std::string bytes(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>()};
    }
} // namespace

TEST_CASE("Export streams disk edits with bounded reads and preserves markers",
          "[streaming-export]")
{
    ExportFiles files;
    GeneratedReader source;
    auto cache = std::make_shared<storage::DecodedBlockCache>(
        storage::AudioBlockBytes * 2);
    auto store =
        std::make_shared<storage::AudioBlockStore>(files.root / "blocks");
    storage::AudioRevisionBuilder builder(source.shape(), store, cache);
    std::vector<float> input(source.shape().frames * 2);
    for (int64_t i = 0; i < source.shape().frames; ++i)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            input[i * 2 + ch] = GeneratedReader::value(i, ch);
        }
    }
    builder.appendInterleaved(input);
    auto imported = storage::AudioEditRevision::from(builder.finish());
    storage::AudioEditTransaction edit(*imported);
    edit.erase(100, 17);
    const auto revision = edit.finish();
    for (const auto extension : {"wav", "aiff", "flac", "m4a"})
    {
        CAPTURE(extension);
        const auto output = files.root / (std::string("export.") + extension);
        auto settings =
            file::defaultExportSettingsForPath(output, SampleFormat::PCM_S16);
        REQUIRE(settings);
        double lastProgress = 0;
        file::AudioFileWriter::writeFile(
            *revision, {{1, 123, "Chapter"}}, output, *settings,
            [&](const std::string &, std::optional<double> progress)
            {
                if (progress)
                {
                    REQUIRE(*progress >= lastProgress);
                    lastProgress = *progress;
                }
            });
        REQUIRE(lastProgress == 1);
        int64_t checked = 0;
        const auto loaded = file::decodeAudioFile(
            output.string(),
            [&](const Document &, int64_t start, const float *samples,
                int64_t count)
            {
                for (int64_t i = 0; i < count; ++i)
                {
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const auto pos = start + i;
                        const float expected = GeneratedReader::value(
                            pos < 100 ? pos : pos + 17, ch);
                        REQUIRE(std::abs(samples[i * 2 + ch] - expected) <=
                                1.0f / 32768);
                    }
                }
                checked += count;
            });
        REQUIRE(checked == revision->shape().frames);
        if (std::string(extension) != "flac")
        {
            REQUIRE(loaded.document.getMarkers().size() == 1);
            REQUIRE(loaded.document.getMarkers()[0].frame == 123);
            REQUIRE(loaded.document.getMarkers()[0].label == "Chapter");
        }
        REQUIRE(cache->stats().peakResidentBytes <=
                storage::AudioBlockBytes * 2);
    }
}

TEST_CASE(
    "Export cancellation and source failure retain destination and clean "
    "temporary files",
    "[streaming-export]")
{
    ExportFiles files;
    for (const auto extension : {"wav", "m4a"})
    {
        for (const bool failRead : {false, true})
        {
            CAPTURE(extension, failRead);
            GeneratedReader source;
            source.failAt = failRead ? 4096 : -1;
            const auto output =
                files.root / (std::string("original.") + extension);
            {
                std::ofstream original(output);
                original << "original bytes";
            }
            const auto settings = file::defaultExportSettingsForPath(
                output, SampleFormat::PCM_S16);
            REQUIRE(settings);
            REQUIRE_THROWS(file::AudioFileWriter::writeFile(
                source, {}, output, *settings,
                [&](const std::string &, std::optional<double> progress)
                {
                    if (!failRead && progress && *progress > 0)
                    {
                        throw std::runtime_error("cancel");
                    }
                }));
            REQUIRE(bytes(output) == "original bytes");
            for (const auto &entry :
                 std::filesystem::directory_iterator(files.root))
            {
                REQUIRE(entry.path().filename().string().find(
                            ".cupuacu.tmp-") == std::string::npos);
            }
            REQUIRE(source.largestRead <=
                    (std::string(extension) == "wav" ? 65536 : 4096));
        }
    }
    const auto original = files.root / "destination";
    {
        std::ofstream output(original);
        output << "keep";
    }
    REQUIRE_THROWS(file::replaceFile(files.root / "missing", original));
    REQUIRE(bytes(original) == "keep");
    const auto replacement = files.root / "replacement";
    {
        std::ofstream output(replacement);
        output << "new";
    }
    file::replaceFile(replacement, original);
    REQUIRE(bytes(original) == "new");
    REQUIRE_FALSE(std::filesystem::exists(replacement));
}

TEST_CASE("Streaming ALAC rejects unsupported durations and propagates aborts",
          "[streaming-export]")
{
    using namespace file::alac;
    const AlacEncodingParameters parameters{48000, 2, 16, 4096};
    unsigned reads = 0, writes = 0;
    const auto reader = [&](uint64_t, uint32_t, std::span<uint8_t> pcm)
    {
        ++reads;
        std::fill(pcm.begin(), pcm.end(), 0);
        return true;
    };
    const auto writer = [&](std::span<const uint8_t>)
    {
        ++writes;
        return false;
    };
    REQUIRE_FALSE(streamEncodedPcmPackets(parameters, uint64_t(INT64_MAX) + 1,
                                          reader, writer));
    REQUIRE(reads == 0);
    REQUIRE_FALSE(streamEncodedPcmPackets(parameters, uint64_t(UINT32_MAX) + 1,
                                          reader, writer));
    REQUIRE(reads == 1);
    REQUIRE(writes == 1);
    reads = writes = 0;
    REQUIRE_FALSE(streamEncodedPcmPackets(parameters, 8193, reader, writer));
    REQUIRE(reads == 1);
    REQUIRE(writes == 1);
    REQUIRE_FALSE(streamEncodedPcmPackets(
        parameters, 8193,
        [](uint64_t, uint32_t, std::span<uint8_t>)
        {
            return false;
        },
        writer));
    REQUIRE(writes == 1);
    const auto offsets =
        file::m4a::wideChunkOffsetAtom({uint64_t(UINT32_MAX) + 4096});
    REQUIRE(std::string(offsets.begin() + 4, offsets.begin() + 8) == "co64");
    REQUIRE(offsets.size() == 24);
    const uint64_t value = uint64_t(UINT32_MAX) + 4096;
    for (int i = 0; i < 8; ++i)
    {
        REQUIRE(offsets[16 + i] == ((value >> ((7 - i) * 8)) & 255));
    }
}

TEST_CASE(
    "Metadata rewrite copies large chunks in bounded reads and detects I/O "
    "errors",
    "[streaming-export]")
{
    struct LimitedReadBuffer : std::stringbuf
    {
        using std::stringbuf::stringbuf;
        std::streamsize xsgetn(char *out, std::streamsize count) override
        {
            if (count > 128 * 1024)
            {
                throw std::runtime_error("unbounded copy");
            }
            return std::stringbuf::xsgetn(out, count);
        }
    };
    std::string payload(1024 * 1024 + 37, ' ');
    for (std::size_t i = 0; i < payload.size(); ++i)
    {
        payload[i] = char(i * 17);
    }
    LimitedReadBuffer source(payload);
    std::istream input(&source);
    std::ostringstream output;
    file::preservation::copyByteRange(input, output, 11, payload.size() - 19,
                                      "copy failed");
    REQUIRE(output.str() == payload.substr(11, payload.size() - 19));
    REQUIRE_THROWS(file::preservation::copyByteRange(
        input, output, payload.size() - 2, 19, "short input"));
    output.setstate(std::ios::badbit);
    REQUIRE_THROWS(
        file::preservation::copyByteRange(input, output, 0, 19, "bad output"));
}

TEST_CASE("Large ALAC export starts with bounded reads and cancels atomically",
          "[streaming-export][m4a-large]")
{
    ExportFiles files;
    GeneratedReader source;
    source.dimensions.frames = int64_t(UINT32_MAX) + 8193;
    const auto output = files.root / "large.m4a";
    { std::ofstream file(output); file << "original"; }
    const auto settings = *file::defaultExportSettingsForPath(output, SampleFormat::PCM_S16);
    unsigned progressCalls = 0;
    REQUIRE_THROWS_AS(file::AudioFileWriter::writeFile(
        source, {}, output, settings, [&](const auto &, auto)
        {
            if (++progressCalls == 3) throw LongTaskCanceledError{};
        }), LongTaskCanceledError);
    REQUIRE(source.largestRead == file::alac::defaultFramesPerPacket());
    REQUIRE(bytes(output) == "original");
    REQUIRE(std::distance(std::filesystem::directory_iterator(files.root),
                          std::filesystem::directory_iterator{}) == 1);
    source.largestRead = 0;
    REQUIRE_THROWS_AS(file::AudioFileWriter::writeFile(
        source, {{1, 0, "Too long"}}, output, settings), std::out_of_range);
    REQUIRE(source.largestRead == 0);
    REQUIRE(bytes(output) == "original");
}

TEST_CASE("M4A parses wide durations and chapter offsets without reading sparse audio",
          "[streaming-export][m4a-large]")
{
    using namespace file::m4a;
    ExportFiles files;
    const auto path = files.root / "wide.m4a";
    const uint64_t frames = uint64_t(UINT32_MAX) + 8193;
    const auto packetFrames = file::alac::defaultFramesPerPacket();
    const auto packet = file::alac::encodePcmPackets(
        {48000, 1, 16, packetFrames}, std::vector<uint8_t>(packetFrames * 2));
    REQUIRE(packet);
    AlacMovieDescription description;
    description.sampleRate = 48000;
    description.frameCount = frames;
    description.framesPerPacket = packetFrames;
    description.sampleEntry = {1, 16, 48000, packet->cookie.bytes};
    description.packetSizes.assign(frames / packetFrames, 5000);
    const uint64_t audioBytes = description.packetSizes.size() * 5000ull;
    REQUIRE(audioBytes > UINT32_MAX);
    const std::vector<DocumentMarker> markers{
        {1, int64_t(frames - 8192), "Past 32 bits"},
        {2, int64_t(frames - 4096), "Last chapter"}};
    {
        std::ofstream output(path, std::ios::binary);
        beginAlacM4a(output);
        output.write(reinterpret_cast<const char *>(packet->bytes.data()), packet->bytes.size());
        output.seekp(ftypAtom().size() + 16 + audioBytes);
        finishAlacM4a(output, std::move(description), audioBytes, markers);
        REQUIRE(output.good());
    }
    const auto parsed = parseAlacM4aFile(path);
    REQUIRE(parsed.frameCount == frames);
    REQUIRE(parsed.packetFrameCounts.size() == frames / packetFrames);
    REQUIRE(parsed.packetOffsets.back() > UINT32_MAX);
    REQUIRE(parsed.markers == markers);
    uint64_t total = 0, decoded = 0;
    REQUIRE_THROWS_AS(streamAlacM4aFile(
        path, [](const uint8_t *, uint32_t, uint32_t count, uint16_t, uint16_t)
        { REQUIRE(count == 4096); }, {},
        [&](uint64_t done, uint64_t expected)
        {
            decoded = done;
            total = expected;
            throw LongTaskCanceledError{};
        }), LongTaskCanceledError);
    REQUIRE(decoded == packetFrames);
    REQUIRE(total == frames);
}

TEST_CASE("ALAC decodes real packets beyond a four GiB sparse prefix",
          "[streaming-export][m4a-large]")
{
    using namespace file::m4a;
    ExportFiles files;
    const auto path = files.root / "offset.m4a";
    const auto packet = file::alac::encodePcmPackets(
        {48000, 1, 16, 4096}, std::vector<uint8_t>(62, 0));
    REQUIRE(packet);
    AlacMovieDescription description;
    description.sampleRate = 48000;
    description.frameCount = 31;
    description.framesPerPacket = 4096;
    description.sampleEntry = {1, 16, 48000, packet->cookie.bytes};
    description.packetSizes = packet->packetSizes;
    description.mdatPayloadOffset = uint64_t(UINT32_MAX) + 4096;
    const auto ftyp = ftypAtom();
    Bytes header;
    appendBe32(header, 1);
    appendFourCc(header, "mdat");
    appendBe64(header, description.mdatPayloadOffset + packet->bytes.size() - ftyp.size());
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char *>(ftyp.data()), ftyp.size());
        output.write(reinterpret_cast<const char *>(header.data()), header.size());
        output.seekp(description.mdatPayloadOffset);
        output.write(reinterpret_cast<const char *>(packet->bytes.data()), packet->bytes.size());
        const auto moov = movieAtom(description);
        output.write(reinterpret_cast<const char *>(moov.data()), moov.size());
        REQUIRE(output.good());
    }
    const auto audio = readAlacM4aFile(path);
    REQUIRE(audio.frameCount == 31);
    REQUIRE(audio.interleavedPcmBytes == std::vector<uint8_t>(62, 0));
}
