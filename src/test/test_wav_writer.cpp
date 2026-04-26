#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestSdlLogSilencer.hpp"
#include "TestPaths.hpp"
#include "actions/io/BackgroundSave.hpp"
#include "actions/Save.hpp"
#include "actions/audio/RecordEdit.hpp"
#include "actions/audio/EditCommands.hpp"
#include "actions/audio/SetSampleValue.hpp"
#include "actions/audio/Trim.hpp"
#include "file/AudioExport.hpp"
#include "file/PreservationBackend.hpp"
#include "file/SaveWritePlan.hpp"
#include "file/SampleQuantization.hpp"
#include "file/SndfilePath.hpp"
#include "file/file_loading.hpp"
#include "file/wav/WavMarkerMetadata.hpp"
#include "file/wav/WavParser.hpp"
#include "file/wav/WavPreservationSupport.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/Window.hpp"

#include <sndfile.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    std::uint32_t float32Bits(const float value)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    void drainPendingSaveWork(cupuacu::State *state)
    {
        for (int attempt = 0; attempt < 5000; ++attempt)
        {
            cupuacu::actions::io::processPendingSaveWork(state);
            if (!state->backgroundSaveJob)
            {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        FAIL("Timed out waiting for background save work");
    }

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

    std::optional<cupuacu::file::AudioExportSettings>
    findWritableUnsupportedMarkerSettings(const cupuacu::Document &document)
    {
        const auto formats = cupuacu::file::probeAvailableExportFormats();
        for (const auto &format : formats)
        {
            for (const auto &encoding : format.encodings)
            {
                cupuacu::file::AudioExportSettings settings{
                    .container = format.container,
                    .codec = format.codec,
                    .majorFormat = format.majorFormat,
                    .subtype = encoding.subtype,
                    .containerLabel = format.containerLabel,
                    .codecLabel = format.codecLabel,
                    .encodingLabel = encoding.label,
                    .extension = encoding.extension,
                };
                settings.compressionLevel =
                    cupuacu::file::defaultCompressionLevelForCodec(format.codec);
                settings.bitrateMode =
                    cupuacu::file::defaultBitrateModeForCodec(format.codec);
                settings.bitrateKbps = cupuacu::file::defaultBitrateKbpsForSettings(
                    settings, document.getSampleRate());

                const auto assessment =
                    cupuacu::file::assessMarkerPersistenceForSettings(document,
                                                                     settings);
                if (assessment.fidelity ==
                    cupuacu::file::MarkerPersistenceFidelity::Unsupported)
                {
                    return settings;
                }
            }
        }

        return std::nullopt;
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

    void writePcm8WavFile(const std::filesystem::path &path,
                          const int sampleRate, const int channels,
                          const std::vector<int8_t> &interleavedSamples,
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
        const uint32_t byteRate = static_cast<uint32_t>(sampleRate * channels);
        appendLe32(fmtChunk, byteRate);
        appendLe16(fmtChunk, static_cast<uint16_t>(channels));
        appendLe16(fmtChunk, 8);
        appendChunk(wavBytes, "fmt ", fmtChunk);

        if (!preDataChunk.empty())
        {
            appendChunk(wavBytes, "JUNK", preDataChunk);
        }

        std::vector<uint8_t> dataChunk;
        dataChunk.reserve(interleavedSamples.size());
        for (const int8_t sample : interleavedSamples)
        {
            appendByte(dataChunk, static_cast<uint8_t>(sample));
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

    void writeFloat32WavFile(const std::filesystem::path &path,
                             const int sampleRate, const int channels,
                             const std::vector<float> &interleavedSamples,
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
        appendLe16(fmtChunk, 3);
        appendLe16(fmtChunk, static_cast<uint16_t>(channels));
        appendLe32(fmtChunk, static_cast<uint32_t>(sampleRate));
        const uint32_t byteRate =
            static_cast<uint32_t>(sampleRate * channels * sizeof(float));
        appendLe32(fmtChunk, byteRate);
        appendLe16(fmtChunk, static_cast<uint16_t>(channels * sizeof(float)));
        appendLe16(fmtChunk, 32);
        appendChunk(wavBytes, "fmt ", fmtChunk);

        if (!preDataChunk.empty())
        {
            appendChunk(wavBytes, "JUNK", preDataChunk);
        }

        std::vector<uint8_t> dataChunk;
        dataChunk.reserve(interleavedSamples.size() * sizeof(float));
        for (const float sample : interleavedSamples)
        {
            appendLe32(dataChunk, float32Bits(sample));
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

    void writeRawBytes(const std::filesystem::path &path,
                       const std::vector<uint8_t> &bytes)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write(reinterpret_cast<const char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        REQUIRE(out.good());
    }

    std::vector<uint8_t> buildRiffWaveBytes(
        const std::vector<std::pair<const char *, std::vector<uint8_t>>> &chunks)
    {
        std::vector<uint8_t> wavBytes;
        appendAscii(wavBytes, "RIFF");
        appendLe32(wavBytes, 0);
        appendAscii(wavBytes, "WAVE");
        for (const auto &[chunkId, payload] : chunks)
        {
            appendChunk(wavBytes, chunkId, payload);
        }

        const uint32_t riffSize = static_cast<uint32_t>(wavBytes.size() - 8);
        wavBytes[4] = static_cast<uint8_t>(riffSize & 0xffu);
        wavBytes[5] = static_cast<uint8_t>((riffSize >> 8) & 0xffu);
        wavBytes[6] = static_cast<uint8_t>((riffSize >> 16) & 0xffu);
        wavBytes[7] = static_cast<uint8_t>((riffSize >> 24) & 0xffu);
        return wavBytes;
    }

    std::vector<uint8_t> makePcm16FmtChunkPayload(const int sampleRate,
                                                  const int channels)
    {
        std::vector<uint8_t> fmtChunk;
        appendLe16(fmtChunk, 1);
        appendLe16(fmtChunk, static_cast<uint16_t>(channels));
        appendLe32(fmtChunk, static_cast<uint32_t>(sampleRate));
        const uint32_t byteRate =
            static_cast<uint32_t>(sampleRate * channels * sizeof(int16_t));
        appendLe32(fmtChunk, byteRate);
        appendLe16(fmtChunk, static_cast<uint16_t>(channels * sizeof(int16_t)));
        appendLe16(fmtChunk, 16);
        return fmtChunk;
    }

    std::vector<uint8_t> readBytes(const std::filesystem::path &path)
    {
        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.good());
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
    }

    std::vector<std::size_t> findDifferingByteOffsets(
        const std::vector<uint8_t> &lhs, const std::vector<uint8_t> &rhs)
    {
        REQUIRE(lhs.size() == rhs.size());

        std::vector<std::size_t> offsets;
        for (std::size_t i = 0; i < lhs.size(); ++i)
        {
            if (lhs[i] != rhs[i])
            {
                offsets.push_back(i);
            }
        }
        return offsets;
    }

    std::vector<float> readFramesAsFloat(const std::filesystem::path &path,
                                         int &sampleRate, int &channels)
    {
        SF_INFO info{};
        SNDFILE *file = cupuacu::file::openSndfile(path, SFM_READ, &info);
        REQUIRE(file != nullptr);

        sampleRate = info.samplerate;
        channels = info.channels;
        std::vector<float> frames(static_cast<size_t>(info.frames * info.channels));
        const sf_count_t readCount =
            sf_readf_float(file, frames.data(), info.frames);
        sf_close(file);
        REQUIRE(readCount == info.frames);
        return frames;
    }

    std::vector<uint8_t> readChunkPayload(const std::filesystem::path &path,
                                          const char (&chunkId)[5])
    {
        const auto parsed = cupuacu::file::wav::WavParser::parseFile(path);
        const auto *chunk = parsed.findChunk(chunkId);
        REQUIRE(chunk != nullptr);

        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.good());
        in.seekg(static_cast<std::streamoff>(chunk->payloadOffset), std::ios::beg);
        std::vector<uint8_t> payload(chunk->payloadSize);
        if (!payload.empty())
        {
            in.read(reinterpret_cast<char *>(payload.data()),
                    static_cast<std::streamsize>(payload.size()));
            REQUIRE(in.good());
        }
        return payload;
    }

    std::vector<uint8_t> sliceBytes(const std::vector<uint8_t> &bytes,
                                    const std::size_t start,
                                    const std::size_t count)
    {
        REQUIRE(start + count <= bytes.size());
        return std::vector<uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(start),
                                    bytes.begin() +
                                        static_cast<std::ptrdiff_t>(start + count));
    }

    std::vector<uint8_t> encodePcm16Samples(const std::vector<std::int16_t> &samples)
    {
        std::vector<uint8_t> bytes;
        bytes.reserve(samples.size() * sizeof(std::int16_t));
        for (const auto sample : samples)
        {
            appendLe16(bytes, static_cast<uint16_t>(sample));
        }
        return bytes;
    }

    std::vector<uint8_t> encodePcm8Samples(const std::vector<std::int8_t> &samples)
    {
        std::vector<uint8_t> bytes;
        bytes.reserve(samples.size());
        for (const auto sample : samples)
        {
            bytes.push_back(static_cast<uint8_t>(sample));
        }
        return bytes;
    }

    std::vector<uint8_t> readChunkBytes(const std::filesystem::path &path,
                                        const char (&chunkId)[5])
    {
        const auto parsed = cupuacu::file::wav::WavParser::parseFile(path);
        const auto *chunk = parsed.findChunk(chunkId);
        REQUIRE(chunk != nullptr);

        return sliceBytes(readBytes(path), chunk->headerOffset,
                          8 + chunk->paddedPayloadSize);
    }

    uint32_t readRiffSizeField(const std::filesystem::path &path)
    {
        const auto bytes = readBytes(path);
        REQUIRE(bytes.size() >= 8);
        return static_cast<uint32_t>(bytes[4]) |
               (static_cast<uint32_t>(bytes[5]) << 8) |
               (static_cast<uint32_t>(bytes[6]) << 16) |
               (static_cast<uint32_t>(bytes[7]) << 24);
    }

    std::vector<std::string> readChunkOrder(const std::filesystem::path &path)
    {
        const auto parsed = cupuacu::file::wav::WavParser::parseFile(path);
        std::vector<std::string> order;
        order.reserve(parsed.chunks.size());
        for (const auto &chunk : parsed.chunks)
        {
            order.emplace_back(chunk.id, chunk.id + 4);
        }
        return order;
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
    REQUIRE(cupuacu::actions::overwritePreserving(&state));

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
    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    REQUIRE(readBytes(wavPath) == originalBytes);
}

TEST_CASE("Overwrite preserves odd-sized chunk bytes including padding", "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-odd-padding-preserve"));
    const auto wavPath = cleanup.path() / "odd_chunks.wav";

    writePcm16WavFile(wavPath, 44100, 1, {100, 200, 300, 400},
                      {'o', 'd', 'd'}, {'t', 'a', 'i'});
    const auto originalJunkChunk = readChunkBytes(wavPath, "JUNK");
    const auto originalListChunk = readChunkBytes(wavPath, "LIST");

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);
    state.getActiveDocumentSession().document.setSample(0, 1, 0.25f);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    REQUIRE(readChunkBytes(wavPath, "JUNK") == originalJunkChunk);
    REQUIRE(readChunkBytes(wavPath, "LIST") == originalListChunk);
}

TEST_CASE("Overwrite after length change keeps RIFF and data sizes consistent",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-size-consistency"));
    const auto wavPath = cleanup.path() / "size_consistency.wav";

    writePcm16WavFile(wavPath, 44100, 1, {100, 200, 300, 400, 500},
                      {'p', 'r', 'e', '!'}, {'p', 'o', 's', 't'});

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);
    state.addAndDoUndoable(
        std::make_shared<cupuacu::actions::audio::Trim>(&state, 1, 3));

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto bytes = readBytes(wavPath);
    const auto parsed = cupuacu::file::wav::WavParser::parseFile(wavPath);
    const auto *dataChunk = parsed.findChunk("data");
    REQUIRE(dataChunk != nullptr);
    REQUIRE(readRiffSizeField(wavPath) == bytes.size() - 8);
    REQUIRE(dataChunk->payloadSize ==
            static_cast<uint32_t>(3 * sizeof(std::int16_t)));
}

TEST_CASE("Overwrite after length change preserves chunk order", "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-chunk-order"));
    const auto wavPath = cleanup.path() / "chunk_order.wav";

    writePcm16WavFile(wavPath, 44100, 1, {100, 200, 300, 400},
                      {'o', 'd', 'd'}, {'t', 'a', 'i'});
    const auto originalOrder = readChunkOrder(wavPath);

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);
    state.getActiveDocumentSession().cursor = 2;
    cupuacu::actions::audio::performInsertSilence(&state, 2);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    REQUIRE(readChunkOrder(wavPath) == originalOrder);
}

TEST_CASE("Overwrite after stereo append keeps RIFF and data sizes consistent",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-stereo-append-size"));
    const auto wavPath = cleanup.path() / "stereo_append.wav";

    writePcm16WavFile(wavPath, 44100, 2, {10, 20, 30, 40, 50, 60, 70, 80},
                      {'o', 'd', 'd'}, {'t', 'a', 'i'});
    const auto originalOrder = readChunkOrder(wavPath);
    const auto originalData = readChunkPayload(wavPath, "data");

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);
    auto &session = state.getActiveDocumentSession();
    session.cursor = state.getActiveDocumentSession().document.getFrameCount();
    cupuacu::actions::audio::performInsertSilence(&state, 2);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto bytes = readBytes(wavPath);
    const auto parsed = cupuacu::file::wav::WavParser::parseFile(wavPath);
    const auto *dataChunk = parsed.findChunk("data");
    REQUIRE(dataChunk != nullptr);
    REQUIRE(readRiffSizeField(wavPath) == bytes.size() - 8);
    REQUIRE(dataChunk->payloadSize ==
            static_cast<uint32_t>(6 * 2 * sizeof(std::int16_t)));
    REQUIRE(readChunkOrder(wavPath) == originalOrder);

    std::vector<uint8_t> expectedData = originalData;
    expectedData.insert(expectedData.end(), 2 * 2 * sizeof(std::int16_t), 0);
    REQUIRE(readChunkPayload(wavPath, "data") == expectedData);
}

TEST_CASE("Overwrite after length change preserves multiple non-audio chunks byte-identically",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-multi-nonaudio-preserve"));
    const auto wavPath = cleanup.path() / "multi_nonaudio.wav";

    const auto wavBytes = buildRiffWaveBytes(
        {{"fmt ", makePcm16FmtChunkPayload(44100, 1)},
         {"JUNK", {'p', 'r', 'e'}},
         {"cue ", {1, 2, 3, 4, 5, 6}},
         {"data", encodePcm16Samples({100, 200, 300, 400})},
         {"LIST", {'p', 'o', 's', 't', '!'}}});
    writeRawBytes(wavPath, wavBytes);
    const auto originalOrder = readChunkOrder(wavPath);
    const auto originalJunkChunk = readChunkBytes(wavPath, "JUNK");
    const auto originalCueChunk = readChunkBytes(wavPath, "cue ");
    const auto originalListChunk = readChunkBytes(wavPath, "LIST");

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);
    auto &session = state.getActiveDocumentSession();
    session.selection.setValue1(1.0);
    session.selection.setValue2(3.0);
    cupuacu::actions::audio::performCut(&state);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    REQUIRE(readChunkOrder(wavPath) == originalOrder);
    REQUIRE(readChunkBytes(wavPath, "JUNK") == originalJunkChunk);
    REQUIRE(readChunkBytes(wavPath, "cue ") == originalCueChunk);
    REQUIRE(readChunkBytes(wavPath, "LIST") == originalListChunk);
}

TEST_CASE("WAV parser exposes chunk table for PCM16 file", "[file]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-wav-parser"));
    const auto wavPath = cleanup.path() / "parsed.wav";

    writePcm16WavFile(wavPath, 48000, 1, {100, -100, 200, -200},
                      {'p', 'r', 'e', '!', 1, 2},
                      {'p', 'o', 's', 't', 9, 8, 7, 6});

    const auto parsed = cupuacu::file::wav::WavParser::parseFile(wavPath);
    REQUIRE(parsed.isWave);
    REQUIRE(parsed.isPcm16);
    REQUIRE(parsed.channelCount == 1);
    REQUIRE(parsed.sampleRate == 48000);
    REQUIRE(parsed.bitsPerSample == 16);
    REQUIRE(parsed.findChunk("fmt ") != nullptr);
    REQUIRE(parsed.findChunk("data") != nullptr);
    REQUIRE(parsed.findChunk("JUNK") != nullptr);
    REQUIRE(parsed.findChunk("LIST") != nullptr);
    REQUIRE(parsed.chunks.size() == 4);
}

TEST_CASE("Loading WAV imports native cue markers into Document", "[file]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-wav-load-markers"));
    const auto wavPath = cleanup.path() / "markers.wav";
    writePcm16WavFile(wavPath, 44100, 1, {1000, 2000, 3000, 4000});
    cupuacu::file::wav::markers::rewriteFileWithMarkers(
        wavPath, {{.id = 11, .frame = 1, .label = "One"},
                  {.id = 12, .frame = 3, .label = "Three"}});

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);

    const auto &markers = state.getActiveDocumentSession().document.getMarkers();
    REQUIRE(markers.size() == 2);
    REQUIRE(markers[0].id == 11);
    REQUIRE(markers[0].frame == 1);
    REQUIRE(markers[0].label == "One");
    REQUIRE(markers[1].id == 12);
    REQUIRE(markers[1].frame == 3);
    REQUIRE(markers[1].label == "Three");
}

TEST_CASE("WAV preservation support reports supported for valid PCM16 overwrite",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-preservation-support-ok"));
    const auto wavPath = cleanup.path() / "supported.wav";

    writePcm16WavFile(wavPath, 44100, 1, {100, 200, 300, 400});

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);

    const auto support =
        cupuacu::file::wav::WavPreservationSupport::assessOverwrite(&state);
    REQUIRE(support.supported);
    REQUIRE(support.reason.empty());
    REQUIRE(state.getActiveDocumentSession().overwritePreservation.available);
    REQUIRE(
        state.getActiveDocumentSession().overwritePreservation.reason.empty());
}

TEST_CASE("WAV preservation support reports structural rejection reason",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-preservation-support-reject"));
    const auto wavPath = cleanup.path() / "unsupported.wav";

    const auto wavBytes = buildRiffWaveBytes(
        {{"fmt ", makePcm16FmtChunkPayload(44100, 1)},
         {"data", encodePcm16Samples({100, 200})},
         {"data", encodePcm16Samples({300, 400})}});
    writeRawBytes(wavPath, wavBytes);

    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();
    session.currentFile = wavPath.string();
    session.currentFileExportSettings = cupuacu::file::AudioExportSettings{
        .container = cupuacu::file::AudioExportContainer::WAV,
        .codec = cupuacu::file::AudioExportCodec::PCM,
        .majorFormat = SF_FORMAT_WAV,
        .subtype = SF_FORMAT_PCM_16,
        .containerLabel = "WAV",
        .codecLabel = "PCM",
        .encodingLabel = "16-bit PCM",
        .extension = "wav",
    };
    session.document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 2);

    const auto support =
        cupuacu::file::wav::WavPreservationSupport::assessOverwrite(&state);
    REQUIRE_FALSE(support.supported);
    REQUIRE(support.reason.find("exactly one data chunk") !=
            std::string::npos);
}

TEST_CASE("Loading valid PCM16 WAV updates session overwrite preservation state",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-session-preservation-state"));
    const auto wavPath = cleanup.path() / "session_state.wav";

    writePcm16WavFile(wavPath, 44100, 1, {100, 200, 300, 400});

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);

    const auto &preservation =
        state.getActiveDocumentSession().overwritePreservation;
    REQUIRE(preservation.available);
    REQUIRE(preservation.reason.empty());
}

TEST_CASE("Save write planner selects preserving overwrite when preservation is supported",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-save-write-plan-preserving"));
    const auto wavPath = cleanup.path() / "plan_preserving.wav";

    writePcm16WavFile(wavPath, 44100, 1, {100, 200, 300, 400});

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);

    const auto settings = cupuacu::file::defaultExportSettingsForPath(
        wavPath, state.getActiveDocumentSession().document.getSampleFormat());
    REQUIRE(settings.has_value());

    const auto plan =
        cupuacu::file::SaveWritePlanner::planPreservingOverwrite(&state, *settings);
    REQUIRE(plan.mode ==
            cupuacu::file::SaveWriteMode::OverwritePreservingRewrite);
    REQUIRE_FALSE(plan.preservationUnavailableReason.has_value());
}

TEST_CASE("Save write planner selects preserving save as when reference and format support it",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-save-write-plan-prefer-preserving"));
    const auto wavPath = cleanup.path() / "plan_prefer_preserving.wav";

    writePcm16WavFile(wavPath, 44100, 1, {100, 200, 300, 400});

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);

    const auto settings = cupuacu::file::defaultExportSettingsForPath(
        wavPath, state.getActiveDocumentSession().document.getSampleFormat());
    REQUIRE(settings.has_value());

    const auto plan =
        cupuacu::file::SaveWritePlanner::planPreservingSaveAs(&state, *settings);
    REQUIRE(plan.mode ==
            cupuacu::file::SaveWriteMode::OverwritePreservingRewrite);
}

TEST_CASE("Save write planner rejects preserving save as for incompatible target format",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-save-write-plan-prefer-rewrite"));
    const auto wavPath = cleanup.path() / "plan_prefer_rewrite.wav";

    writePcm16WavFile(wavPath, 44100, 1, {100, 200, 300, 400});

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);

    const auto settings = cupuacu::file::AudioExportSettings{
        .container = cupuacu::file::AudioExportContainer::FLAC,
        .codec = cupuacu::file::AudioExportCodec::FLAC,
        .majorFormat = SF_FORMAT_FLAC,
        .subtype = SF_FORMAT_PCM_16,
        .containerLabel = "FLAC",
        .codecLabel = "FLAC",
        .encodingLabel = "16-bit PCM",
        .extension = "flac",
    };

    const auto plan =
        cupuacu::file::SaveWritePlanner::planPreservingSaveAs(&state, settings);
    REQUIRE(plan.mode ==
            cupuacu::file::SaveWriteMode::PreservationRequiredButUnavailable);
    REQUIRE(plan.preservationUnavailableReason.has_value());
}

TEST_CASE("Preservation backend dispatch selects WAV and rejects unsupported formats",
          "[file]")
{
    const auto wavSettings = cupuacu::file::AudioExportSettings{
        .container = cupuacu::file::AudioExportContainer::WAV,
        .codec = cupuacu::file::AudioExportCodec::PCM,
        .majorFormat = SF_FORMAT_WAV,
        .subtype = SF_FORMAT_PCM_16,
        .containerLabel = "WAV",
        .codecLabel = "PCM",
        .encodingLabel = "16-bit PCM",
        .extension = "wav",
    };
    REQUIRE(cupuacu::file::preservationBackendKindForSettings(wavSettings) ==
            cupuacu::file::PreservationBackendKind::WavPcm);

    const auto wavPcm8Settings = cupuacu::file::AudioExportSettings{
        .container = cupuacu::file::AudioExportContainer::WAV,
        .codec = cupuacu::file::AudioExportCodec::PCM,
        .majorFormat = SF_FORMAT_WAV,
        .subtype = SF_FORMAT_PCM_S8,
        .containerLabel = "WAV",
        .codecLabel = "PCM",
        .encodingLabel = "8-bit PCM",
        .extension = "wav",
    };
    REQUIRE(cupuacu::file::preservationBackendKindForSettings(wavPcm8Settings) ==
            cupuacu::file::PreservationBackendKind::WavPcm);

    const auto flacSettings = cupuacu::file::AudioExportSettings{
        .container = cupuacu::file::AudioExportContainer::FLAC,
        .codec = cupuacu::file::AudioExportCodec::FLAC,
        .majorFormat = SF_FORMAT_FLAC,
        .subtype = SF_FORMAT_PCM_16,
        .containerLabel = "FLAC",
        .codecLabel = "FLAC",
        .encodingLabel = "16-bit PCM",
        .extension = "flac",
    };
    REQUIRE(cupuacu::file::preservationBackendKindForSettings(flacSettings) ==
            cupuacu::file::PreservationBackendKind::None);

    const auto support =
        cupuacu::file::assessPreservationAgainstReference(nullptr, flacSettings);
    REQUIRE_FALSE(support.available);
    REQUIRE(support.reason == "State is null");
}

TEST_CASE("WAV preservation support reports supported for valid PCM8 overwrite",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-pcm8-preservation-support"));
    const auto wavPath = cleanup.path() / "preserve_pcm8.wav";

    writePcm8WavFile(wavPath, 44100, 1, {10, 20, 30, 40});

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);

    const auto support =
        cupuacu::file::wav::WavPreservationSupport::assessOverwrite(&state);
    REQUIRE(support.supported);
    REQUIRE(support.reason.empty());
}

TEST_CASE("WAV preservation support reports supported for valid float32 overwrite",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-f32-preservation-support"));
    const auto wavPath = cleanup.path() / "preserve_f32.wav";

    writeFloat32WavFile(wavPath, 44100, 1, {0.0f, 0.25f, -0.25f, 0.5f});

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);

    const auto support =
        cupuacu::file::wav::WavPreservationSupport::assessOverwrite(&state);
    REQUIRE(support.supported);
    REQUIRE(support.reason.empty());
}

TEST_CASE("Save write planner reports unavailable preservation when overwrite cannot preserve",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-save-write-plan-fallback"));
    const auto wavPath = cleanup.path() / "plan_fallback.wav";

    writePcm16WavFile(wavPath, 44100, 1, {100, 200, 300, 400});

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);

    auto settings = cupuacu::file::defaultExportSettingsForPath(
        wavPath, state.getActiveDocumentSession().document.getSampleFormat());
    REQUIRE(settings.has_value());

    state.getActiveDocumentSession().breakOverwritePreservation(
        "Recording changed channel count");

    const auto plan =
        cupuacu::file::SaveWritePlanner::planPreservingOverwrite(&state, *settings);
    REQUIRE(plan.mode ==
            cupuacu::file::SaveWriteMode::PreservationRequiredButUnavailable);
    REQUIRE(plan.preservationUnavailableReason.has_value());
    REQUIRE(plan.preservationUnavailableReason->find("channel count") !=
            std::string::npos);
}

TEST_CASE("Overwrite rejects WAV files with multiple data chunks", "[file]")
{
    cupuacu::test::ScopedSdlLogSilencer silenceLogs;
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-multiple-data-chunks"));
    const auto wavPath = cleanup.path() / "multiple_data.wav";

    const auto wavBytes = buildRiffWaveBytes(
        {{"fmt ", makePcm16FmtChunkPayload(44100, 1)},
         {"data", encodePcm16Samples({100, 200})},
         {"data", encodePcm16Samples({300, 400})}});
    writeRawBytes(wavPath, wavBytes);
    const auto originalBytes = readBytes(wavPath);

    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();
    session.currentFile = wavPath.string();
    session.currentFileExportSettings = cupuacu::file::AudioExportSettings{
        .container = cupuacu::file::AudioExportContainer::WAV,
        .codec = cupuacu::file::AudioExportCodec::PCM,
        .majorFormat = SF_FORMAT_WAV,
        .subtype = SF_FORMAT_PCM_16,
        .containerLabel = "WAV",
        .codecLabel = "PCM",
        .encodingLabel = "16-bit PCM",
        .extension = "wav",
    };
    session.document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 2);

    std::string reportedMessage;
    state.errorReporter = [&](const std::string &, const std::string &message)
    {
        reportedMessage = message;
    };

    REQUIRE_FALSE(cupuacu::actions::overwritePreserving(&state));
    REQUIRE(reportedMessage.find("exactly one data chunk") != std::string::npos);
    REQUIRE(readBytes(wavPath) == originalBytes);
}

TEST_CASE("Overwrite rejects WAV files without fmt chunk", "[file]")
{
    cupuacu::test::ScopedSdlLogSilencer silenceLogs;
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-wav-missing-fmt"));
    const auto wavPath = cleanup.path() / "missing_fmt.wav";

    const auto wavBytes = buildRiffWaveBytes(
        {{"data", encodePcm16Samples({100, 200, 300, 400})}});
    writeRawBytes(wavPath, wavBytes);
    const auto originalBytes = readBytes(wavPath);

    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();
    session.currentFile = wavPath.string();
    session.currentFileExportSettings = cupuacu::file::AudioExportSettings{
        .container = cupuacu::file::AudioExportContainer::WAV,
        .codec = cupuacu::file::AudioExportCodec::PCM,
        .majorFormat = SF_FORMAT_WAV,
        .subtype = SF_FORMAT_PCM_16,
        .containerLabel = "WAV",
        .codecLabel = "PCM",
        .encodingLabel = "16-bit PCM",
        .extension = "wav",
    };
    session.document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 2);

    std::string reportedMessage;
    state.errorReporter = [&](const std::string &, const std::string &message)
    {
        reportedMessage = message;
    };

    REQUIRE_FALSE(cupuacu::actions::overwritePreserving(&state));
    REQUIRE(reportedMessage.find("Source WAV format does not match document format") !=
            std::string::npos);
    REQUIRE(readBytes(wavPath) == originalBytes);
}

TEST_CASE("Overwrite rejects WAV files with multiple fmt chunks", "[file]")
{
    cupuacu::test::ScopedSdlLogSilencer silenceLogs;
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-multiple-fmt-chunks"));
    const auto wavPath = cleanup.path() / "multiple_fmt.wav";

    const auto wavBytes = buildRiffWaveBytes(
        {{"fmt ", makePcm16FmtChunkPayload(44100, 1)},
         {"fmt ", makePcm16FmtChunkPayload(44100, 1)},
         {"data", encodePcm16Samples({100, 200})}});
    writeRawBytes(wavPath, wavBytes);
    const auto originalBytes = readBytes(wavPath);

    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();
    session.currentFile = wavPath.string();
    session.currentFileExportSettings = cupuacu::file::AudioExportSettings{
        .container = cupuacu::file::AudioExportContainer::WAV,
        .codec = cupuacu::file::AudioExportCodec::PCM,
        .majorFormat = SF_FORMAT_WAV,
        .subtype = SF_FORMAT_PCM_16,
        .containerLabel = "WAV",
        .codecLabel = "PCM",
        .encodingLabel = "16-bit PCM",
        .extension = "wav",
    };
    session.document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 2);

    std::string reportedMessage;
    state.errorReporter = [&](const std::string &, const std::string &message)
    {
        reportedMessage = message;
    };

    REQUIRE_FALSE(cupuacu::actions::overwritePreserving(&state));
    REQUIRE(reportedMessage.find("exactly one fmt chunk") != std::string::npos);
    REQUIRE(readBytes(wavPath) == originalBytes);
}

TEST_CASE("Overwrite rejects WAV files with truncated chunk payload", "[file]")
{
    cupuacu::test::ScopedSdlLogSilencer silenceLogs;
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-truncated-chunk"));
    const auto wavPath = cleanup.path() / "truncated.wav";

    auto wavBytes = buildRiffWaveBytes(
        {{"fmt ", makePcm16FmtChunkPayload(44100, 1)},
         {"data", encodePcm16Samples({100, 200})}});
    wavBytes.resize(wavBytes.size() - sizeof(std::int16_t));
    const uint32_t riffSize = static_cast<uint32_t>(wavBytes.size() - 8);
    wavBytes[4] = static_cast<uint8_t>(riffSize & 0xffu);
    wavBytes[5] = static_cast<uint8_t>((riffSize >> 8) & 0xffu);
    wavBytes[6] = static_cast<uint8_t>((riffSize >> 16) & 0xffu);
    wavBytes[7] = static_cast<uint8_t>((riffSize >> 24) & 0xffu);
    writeRawBytes(wavPath, wavBytes);
    const auto originalBytes = readBytes(wavPath);

    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();
    session.currentFile = wavPath.string();
    session.currentFileExportSettings = cupuacu::file::AudioExportSettings{
        .container = cupuacu::file::AudioExportContainer::WAV,
        .codec = cupuacu::file::AudioExportCodec::PCM,
        .majorFormat = SF_FORMAT_WAV,
        .subtype = SF_FORMAT_PCM_16,
        .containerLabel = "WAV",
        .codecLabel = "PCM",
        .encodingLabel = "16-bit PCM",
        .extension = "wav",
    };
    session.document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 2);

    std::string reportedMessage;
    state.errorReporter = [&](const std::string &, const std::string &message)
    {
        reportedMessage = message;
    };

    REQUIRE_FALSE(cupuacu::actions::overwritePreserving(&state));
    REQUIRE(reportedMessage.find("extends past end of file") !=
            std::string::npos);
    REQUIRE(readBytes(wavPath) == originalBytes);
}

TEST_CASE("Overwrite rejects WAV files with inconsistent RIFF size", "[file]")
{
    cupuacu::test::ScopedSdlLogSilencer silenceLogs;
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-bad-riff-size"));
    const auto wavPath = cleanup.path() / "bad_riff_size.wav";

    auto wavBytes = buildRiffWaveBytes(
        {{"fmt ", makePcm16FmtChunkPayload(44100, 1)},
         {"data", encodePcm16Samples({100, 200})}});
    appendByte(wavBytes, 0x7f);
    writeRawBytes(wavPath, wavBytes);
    const auto originalBytes = readBytes(wavPath);

    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();
    session.currentFile = wavPath.string();
    session.currentFileExportSettings = cupuacu::file::AudioExportSettings{
        .container = cupuacu::file::AudioExportContainer::WAV,
        .codec = cupuacu::file::AudioExportCodec::PCM,
        .majorFormat = SF_FORMAT_WAV,
        .subtype = SF_FORMAT_PCM_16,
        .containerLabel = "WAV",
        .codecLabel = "PCM",
        .encodingLabel = "16-bit PCM",
        .extension = "wav",
    };
    session.document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 2);

    std::string reportedMessage;
    state.errorReporter = [&](const std::string &, const std::string &message)
    {
        reportedMessage = message;
    };

    REQUIRE_FALSE(cupuacu::actions::overwritePreserving(&state));
    REQUIRE(reportedMessage.find("RIFF size field does not match file size") !=
            std::string::npos);
    REQUIRE(readBytes(wavPath) == originalBytes);
}

TEST_CASE("Overwrite patches only one mono PCM16 sample in place", "[file]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-wav-mono-patch"));
    const auto wavPath = cleanup.path() / "mono.wav";

    writePcm16WavFile(wavPath, 44100, 1, {0, 1000, -1000, 2000});
    const auto originalBytes = readBytes(wavPath);

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);
    state.getActiveDocumentSession().document.setSample(0, 2, 0.25f);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto updatedBytes = readBytes(wavPath);
    const auto differingOffsets =
        findDifferingByteOffsets(originalBytes, updatedBytes);
    REQUIRE(differingOffsets.size() == sizeof(std::int16_t));
}

TEST_CASE("Overwrite patches only one mono PCM8 sample in place", "[file]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-wav-pcm8-patch"));
    const auto wavPath = cleanup.path() / "mono_pcm8.wav";

    writePcm8WavFile(wavPath, 44100, 1, {0, 10, -10, 20});
    const auto originalBytes = readBytes(wavPath);

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);
    state.getActiveDocumentSession().document.setSample(0, 2, 0.25f);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto updatedBytes = readBytes(wavPath);
    const auto differingOffsets =
        findDifferingByteOffsets(originalBytes, updatedBytes);
    REQUIRE(differingOffsets.size() == 1);
}

TEST_CASE("Overwrite patches only one mono float32 sample in place", "[file]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-wav-f32-patch"));
    const auto wavPath = cleanup.path() / "mono_f32.wav";

    writeFloat32WavFile(wavPath, 44100, 1, {0.0f, 0.25f, -0.25f, 0.5f});
    const auto originalBytes = readBytes(wavPath);

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);
    state.getActiveDocumentSession().document.setSample(0, 2, 0.75f);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto updatedBytes = readBytes(wavPath);
    const auto differingOffsets =
        findDifferingByteOffsets(originalBytes, updatedBytes);
    const auto parsed = cupuacu::file::wav::WavParser::parseFile(wavPath);
    const auto *dataChunk = parsed.findChunk("data");
    REQUIRE(dataChunk != nullptr);
    const std::size_t sampleOffset =
        dataChunk->payloadOffset + (2 * sizeof(float));
    REQUIRE_FALSE(differingOffsets.empty());
    REQUIRE(differingOffsets.size() <= sizeof(float));
    for (const auto offset : differingOffsets)
    {
        REQUIRE(offset >= sampleOffset);
        REQUIRE(offset < sampleOffset + sizeof(float));
    }
}

TEST_CASE("Overwrite patches only one stereo channel sample in place", "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-stereo-channel-patch"));
    const auto wavPath = cleanup.path() / "stereo.wav";

    writePcm16WavFile(wavPath, 44100, 2, {10, 20, 30, 40, 50, 60});
    const auto originalBytes = readBytes(wavPath);

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);
    state.getActiveDocumentSession().document.setSample(1, 1, -0.5f);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto updatedBytes = readBytes(wavPath);
    const auto differingOffsets =
        findDifferingByteOffsets(originalBytes, updatedBytes);
    REQUIRE(differingOffsets.size() == sizeof(std::int16_t));
}

TEST_CASE("Overwrite after trim preserves surviving PCM16 sample bytes",
          "[file]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-wav-trim-preserve"));
    const auto wavPath = cleanup.path() / "trim.wav";

    writePcm16WavFile(wavPath, 44100, 1, {100, 200, 300, 400, 500},
                      {'p', 'r', 'e', '!'}, {'p', 'o', 's', 't'});
    const auto originalData = readChunkPayload(wavPath, "data");

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);
    state.addAndDoUndoable(
        std::make_shared<cupuacu::actions::audio::Trim>(&state, 1, 3));

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto updatedData = readChunkPayload(wavPath, "data");
    REQUIRE(updatedData ==
            sliceBytes(originalData, sizeof(std::int16_t) * 1,
                       sizeof(std::int16_t) * 3));
    REQUIRE(readChunkPayload(wavPath, "JUNK") ==
            std::vector<uint8_t>({'p', 'r', 'e', '!'}));
    REQUIRE(readChunkPayload(wavPath, "LIST") ==
            std::vector<uint8_t>({'p', 'o', 's', 't'}));
}

TEST_CASE("Overwrite after trim preserves surviving PCM8 sample bytes",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-pcm8-trim-preserve"));
    const auto wavPath = cleanup.path() / "trim_pcm8.wav";

    writePcm8WavFile(wavPath, 44100, 1, {10, 20, 30, 40, 50},
                     {'p', 'r', 'e', '!'}, {'p', 'o', 's', 't'});
    const auto originalData = readChunkPayload(wavPath, "data");

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);
    state.addAndDoUndoable(
        std::make_shared<cupuacu::actions::audio::Trim>(&state, 1, 3));

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto updatedData = readChunkPayload(wavPath, "data");
    REQUIRE(updatedData == sliceBytes(originalData, 1, 3));
    REQUIRE(readChunkPayload(wavPath, "JUNK") ==
            std::vector<uint8_t>({'p', 'r', 'e', '!'}));
    REQUIRE(readChunkPayload(wavPath, "LIST") ==
            std::vector<uint8_t>({'p', 'o', 's', 't'}));
}

TEST_CASE("Overwrite after cut preserves surviving PCM16 sample bytes",
          "[file]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-wav-cut-preserve"));
    const auto wavPath = cleanup.path() / "cut.wav";

    writePcm16WavFile(wavPath, 44100, 1, {100, 200, 300, 400, 500, 600},
                      {'p', 'r', 'e', '!'}, {'p', 'o', 's', 't'});
    const auto originalData = readChunkPayload(wavPath, "data");

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);
    auto &session = state.getActiveDocumentSession();
    session.selection.setValue1(2.0);
    session.selection.setValue2(4.0);
    cupuacu::actions::audio::performCut(&state);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto updatedData = readChunkPayload(wavPath, "data");
    std::vector<uint8_t> expectedData;
    expectedData.insert(expectedData.end(), originalData.begin(),
                        originalData.begin() +
                            static_cast<std::ptrdiff_t>(sizeof(std::int16_t) * 2));
    expectedData.insert(expectedData.end(),
                        originalData.begin() +
                            static_cast<std::ptrdiff_t>(sizeof(std::int16_t) * 4),
                        originalData.end());
    REQUIRE(updatedData == expectedData);
    REQUIRE(readChunkPayload(wavPath, "JUNK") ==
            std::vector<uint8_t>({'p', 'r', 'e', '!'}));
    REQUIRE(readChunkPayload(wavPath, "LIST") ==
            std::vector<uint8_t>({'p', 'o', 's', 't'}));
}

TEST_CASE("Overwrite after paste of copied original material preserves source PCM16 sample bytes",
          "[file]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-wav-paste-preserve"));
    const auto wavPath = cleanup.path() / "paste.wav";

    writePcm16WavFile(wavPath, 44100, 1, {100, 200, 300, 400, 500},
                      {'p', 'r', 'e', '!'}, {'p', 'o', 's', 't'});
    const auto originalData = readChunkPayload(wavPath, "data");

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);
    auto &session = state.getActiveDocumentSession();

    session.selection.setValue1(1.0);
    session.selection.setValue2(3.0);
    cupuacu::actions::audio::performCopy(&state);
    session.selection.reset();
    session.cursor = 5;
    cupuacu::actions::audio::performPaste(&state);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto updatedData = readChunkPayload(wavPath, "data");
    std::vector<uint8_t> expectedData = originalData;
    expectedData.insert(expectedData.end(),
                        originalData.begin() +
                            static_cast<std::ptrdiff_t>(sizeof(std::int16_t) * 1),
                        originalData.begin() +
                            static_cast<std::ptrdiff_t>(sizeof(std::int16_t) * 3));
    REQUIRE(updatedData == expectedData);
    REQUIRE(readChunkPayload(wavPath, "JUNK") ==
            std::vector<uint8_t>({'p', 'r', 'e', '!'}));
    REQUIRE(readChunkPayload(wavPath, "LIST") ==
            std::vector<uint8_t>({'p', 'o', 's', 't'}));
}

TEST_CASE("Overwrite after insert silence preserves surrounding PCM16 sample bytes",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-insert-silence-preserve"));
    const auto wavPath = cleanup.path() / "insert_silence.wav";

    writePcm16WavFile(wavPath, 44100, 1, {100, 200, 300, 400},
                      {'p', 'r', 'e', '!'}, {'p', 'o', 's', 't'});
    const auto originalData = readChunkPayload(wavPath, "data");

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);
    auto &session = state.getActiveDocumentSession();
    session.cursor = 2;
    cupuacu::actions::audio::performInsertSilence(&state, 2);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto updatedData = readChunkPayload(wavPath, "data");
    std::vector<uint8_t> expectedData;
    expectedData.insert(expectedData.end(), originalData.begin(),
                        originalData.begin() +
                            static_cast<std::ptrdiff_t>(sizeof(std::int16_t) * 2));
    expectedData.insert(expectedData.end(), 2 * sizeof(std::int16_t), 0);
    expectedData.insert(expectedData.end(),
                        originalData.begin() +
                            static_cast<std::ptrdiff_t>(sizeof(std::int16_t) * 2),
                        originalData.end());
    REQUIRE(updatedData == expectedData);
    REQUIRE(readChunkPayload(wavPath, "JUNK") ==
            std::vector<uint8_t>({'p', 'r', 'e', '!'}));
    REQUIRE(readChunkPayload(wavPath, "LIST") ==
            std::vector<uint8_t>({'p', 'o', 's', 't'}));
}

TEST_CASE("Overwrite after trim preserves dirty survivors and clean survivors distinctly",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-trim-dirty-survivor"));
    const auto wavPath = cleanup.path() / "trim_dirty.wav";

    writePcm16WavFile(wavPath, 44100, 1, {100, 200, 300, 400, 500},
                      {'p', 'r', 'e', '!'}, {'p', 'o', 's', 't'});
    const auto originalData = readChunkPayload(wavPath, "data");

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);
    auto &document = state.getActiveDocumentSession().document;
    document.setSample(0, 2, 0.25f);
    state.addAndDoUndoable(
        std::make_shared<cupuacu::actions::audio::Trim>(&state, 1, 3));

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto updatedData = readChunkPayload(wavPath, "data");
    const auto dirtySample = static_cast<std::int16_t>(
        cupuacu::file::quantizeIntegerPcmSample(cupuacu::SampleFormat::PCM_S16,
                                                0.25f, false));
    std::vector<uint8_t> expectedData;
    expectedData.insert(expectedData.end(),
                        originalData.begin() +
                            static_cast<std::ptrdiff_t>(sizeof(std::int16_t) * 1),
                        originalData.begin() +
                            static_cast<std::ptrdiff_t>(sizeof(std::int16_t) * 2));
    const auto dirtyBytes = encodePcm16Samples({dirtySample});
    expectedData.insert(expectedData.end(), dirtyBytes.begin(), dirtyBytes.end());
    expectedData.insert(expectedData.end(),
                        originalData.begin() +
                            static_cast<std::ptrdiff_t>(sizeof(std::int16_t) * 3),
                        originalData.begin() +
                            static_cast<std::ptrdiff_t>(sizeof(std::int16_t) * 4));
    REQUIRE(updatedData == expectedData);
    REQUIRE(readChunkPayload(wavPath, "JUNK") ==
            std::vector<uint8_t>({'p', 'r', 'e', '!'}));
    REQUIRE(readChunkPayload(wavPath, "LIST") ==
            std::vector<uint8_t>({'p', 'o', 's', 't'}));
}

TEST_CASE("Overwrite after stereo cut preserves surviving interleaved PCM16 sample bytes",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-stereo-cut-preserve"));
    const auto wavPath = cleanup.path() / "stereo_cut.wav";

    writePcm16WavFile(wavPath, 44100, 2, {10, 20, 30, 40, 50, 60, 70, 80},
                      {'p', 'r', 'e', '!'}, {'p', 'o', 's', 't'});
    const auto originalData = readChunkPayload(wavPath, "data");

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);
    auto &session = state.getActiveDocumentSession();
    session.selection.setValue1(1.0);
    session.selection.setValue2(3.0);
    cupuacu::actions::audio::performCut(&state);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto updatedData = readChunkPayload(wavPath, "data");
    std::vector<uint8_t> expectedData;
    expectedData.insert(expectedData.end(), originalData.begin(),
                        originalData.begin() +
                            static_cast<std::ptrdiff_t>(sizeof(std::int16_t) * 2));
    expectedData.insert(expectedData.end(),
                        originalData.begin() +
                            static_cast<std::ptrdiff_t>(sizeof(std::int16_t) * 6),
                        originalData.end());
    REQUIRE(updatedData == expectedData);
    REQUIRE(readChunkPayload(wavPath, "JUNK") ==
            std::vector<uint8_t>({'p', 'r', 'e', '!'}));
    REQUIRE(readChunkPayload(wavPath, "LIST") ==
            std::vector<uint8_t>({'p', 'o', 's', 't'}));
}

TEST_CASE("Overwrite after record-style overwrite patches recorded PCM16 bytes",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-record-overwrite"));
    const auto wavPath = cleanup.path() / "record_overwrite.wav";

    writePcm16WavFile(wavPath, 44100, 2, {10, 20, 30, 40, 50, 60, 70, 80},
                      {'p', 'r', 'e', '!'}, {'p', 'o', 's', 't'});
    const auto originalBytes = readBytes(wavPath);

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);

    cupuacu::actions::audio::RecordEditData data;
    data.startFrame = 1;
    data.endFrame = 3;
    data.oldFrameCount = 4;
    data.oldChannelCount = 2;
    data.targetChannelCount = 2;
    data.oldSampleRate = 44100;
    data.newSampleRate = 44100;
    data.oldFormat = cupuacu::SampleFormat::PCM_S16;
    data.newFormat = cupuacu::SampleFormat::PCM_S16;
    data.oldCursor = 1;
    data.newCursor = 3;
    data.overwrittenOldSamples = {{30.0f / 32767.0f, 50.0f / 32767.0f},
                                  {40.0f / 32767.0f, 60.0f / 32767.0f}};
    data.recordedSamples = {{0.25f, -0.25f}, {-0.5f, 0.5f}};

    state.addAndDoUndoable(
        std::make_shared<cupuacu::actions::audio::RecordEdit>(&state, data));

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto updatedBytes = readBytes(wavPath);
    REQUIRE(updatedBytes != originalBytes);

    int sampleRate = 0;
    int channels = 0;
    const auto frames = readFramesAsFloat(wavPath, sampleRate, channels);
    REQUIRE(sampleRate == 44100);
    REQUIRE(channels == 2);
    REQUIRE(frames.size() == 8);
    REQUIRE(frames[2] == Catch::Approx(0.25f).margin(1.0f / 32767.0f));
    REQUIRE(frames[3] == Catch::Approx(-0.5f).margin(1.0f / 32767.0f));
    REQUIRE(frames[4] == Catch::Approx(-0.25f).margin(1.0f / 32767.0f));
    REQUIRE(frames[5] == Catch::Approx(0.5f).margin(1.0f / 32767.0f));
}

TEST_CASE("Overwrite after record-style append preserves original prefix and appends new PCM16 bytes",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-record-append"));
    const auto wavPath = cleanup.path() / "record_append.wav";

    writePcm16WavFile(wavPath, 44100, 1, {100, 200, 300, 400},
                      {'p', 'r', 'e', '!'}, {'p', 'o', 's', 't'});
    const auto originalData = readChunkPayload(wavPath, "data");

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);

    cupuacu::actions::audio::RecordEditData data;
    data.startFrame = 4;
    data.endFrame = 6;
    data.oldFrameCount = 4;
    data.oldChannelCount = 1;
    data.targetChannelCount = 1;
    data.oldSampleRate = 44100;
    data.newSampleRate = 44100;
    data.oldFormat = cupuacu::SampleFormat::PCM_S16;
    data.newFormat = cupuacu::SampleFormat::PCM_S16;
    data.oldCursor = 4;
    data.newCursor = 6;
    data.overwrittenOldSamples = {{}};
    data.recordedSamples = {{0.25f, -0.25f}};

    state.addAndDoUndoable(
        std::make_shared<cupuacu::actions::audio::RecordEdit>(&state, data));

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto updatedData = readChunkPayload(wavPath, "data");
    const auto appendedSamples = encodePcm16Samples(
        {static_cast<std::int16_t>(cupuacu::file::quantizeIntegerPcmSample(
             cupuacu::SampleFormat::PCM_S16, 0.25f, false)),
         static_cast<std::int16_t>(cupuacu::file::quantizeIntegerPcmSample(
             cupuacu::SampleFormat::PCM_S16, -0.25f, false))});
    std::vector<uint8_t> expectedData = originalData;
    expectedData.insert(expectedData.end(), appendedSamples.begin(),
                        appendedSamples.end());
    REQUIRE(updatedData == expectedData);
    REQUIRE(readChunkPayload(wavPath, "JUNK") ==
            std::vector<uint8_t>({'p', 'r', 'e', '!'}));
    REQUIRE(readChunkPayload(wavPath, "LIST") ==
            std::vector<uint8_t>({'p', 'o', 's', 't'}));
}

TEST_CASE("Record edit that changes channel count breaks overwrite preservation until undo",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-record-breaks-preservation"));
    const auto wavPath = cleanup.path() / "record_breaks_preservation.wav";

    writePcm16WavFile(wavPath, 44100, 1, {100, 200, 300, 400});

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);
    REQUIRE(state.getActiveDocumentSession().overwritePreservation.available);

    cupuacu::actions::audio::RecordEditData data;
    data.startFrame = 1;
    data.endFrame = 3;
    data.oldFrameCount = 4;
    data.oldChannelCount = 1;
    data.targetChannelCount = 2;
    data.oldSampleRate = 44100;
    data.newSampleRate = 44100;
    data.oldFormat = cupuacu::SampleFormat::PCM_S16;
    data.newFormat = cupuacu::SampleFormat::PCM_S16;
    data.oldCursor = 1;
    data.newCursor = 3;
    data.overwrittenOldSamples = {{200.0f / 32767.0f, 300.0f / 32767.0f}};
    data.recordedSamples = {{0.25f, -0.25f}, {-0.5f, 0.5f}};

    state.addAndDoUndoable(
        std::make_shared<cupuacu::actions::audio::RecordEdit>(&state, data));

    const auto &broken = state.getActiveDocumentSession().overwritePreservation;
    REQUIRE_FALSE(broken.available);
    REQUIRE(broken.reason.find("channel count") != std::string::npos);

    state.undo();

    const auto &restored =
        state.getActiveDocumentSession().overwritePreservation;
    REQUIRE(restored.available);
    REQUIRE(restored.reason.empty());
}

TEST_CASE("Sample value edit keeps overwrite preservation available", "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-sample-edit-preservation"));
    const auto wavPath = cleanup.path() / "sample_edit_preservation.wav";

    writePcm16WavFile(wavPath, 44100, 1, {100, 200, 300, 400});

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);

    REQUIRE(state.getActiveDocumentSession().overwritePreservation.available);

    auto &document = state.getActiveDocumentSession().document;
    const auto oldValue = document.getSample(0, 1);
    auto undoable = std::make_shared<cupuacu::actions::audio::SetSampleValue>(
        &state, 0, 1, oldValue);
    undoable->setNewValue(0.25f);
    state.addAndDoUndoable(undoable);

    const auto &preservation =
        state.getActiveDocumentSession().overwritePreservation;
    REQUIRE(preservation.available);
    REQUIRE(preservation.reason.empty());
}

TEST_CASE("Trim keeps overwrite preservation available", "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-trim-preservation"));
    const auto wavPath = cleanup.path() / "trim_preservation.wav";

    writePcm16WavFile(wavPath, 44100, 1, {100, 200, 300, 400});

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);

    REQUIRE(state.getActiveDocumentSession().overwritePreservation.available);

    state.addAndDoUndoable(
        std::make_shared<cupuacu::actions::audio::Trim>(&state, 1, 2));

    const auto &preservation =
        state.getActiveDocumentSession().overwritePreservation;
    REQUIRE(preservation.available);
    REQUIRE(preservation.reason.empty());
    REQUIRE(cupuacu::actions::overwritePreserving(&state));
}

TEST_CASE("Second overwrite after edit is byte-identical", "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-second-overwrite"));
    const auto wavPath = cleanup.path() / "second.wav";

    writePcm16WavFile(wavPath, 44100, 1, {0, 1000, 2000, 3000});

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);
    state.getActiveDocumentSession().document.setSample(0, 1, -0.25f);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));
    const auto savedBytes = readBytes(wavPath);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));
    REQUIRE(readBytes(wavPath) == savedBytes);
}

TEST_CASE("Overwrite clips edited samples into valid PCM16 range", "[file]")
{
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

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    int sampleRate = 0;
    int channels = 0;
    const auto frames = readFramesAsFloat(wavPath, sampleRate, channels);
    REQUIRE(sampleRate == 22050);
    REQUIRE(channels == 1);
    REQUIRE(frames.size() == 3);
    REQUIRE(frames[0] == Catch::Approx(1.0f).margin(1.0f / 32767.0f));
    REQUIRE(frames[1] == Catch::Approx(-1.0f).margin(1.0f / 32767.0f));
    REQUIRE(frames[2] == Catch::Approx(0.5f).margin(1.0f / 32767.0f));
}

TEST_CASE("Save as writes a new WAV file and updates active file state", "[file]")
{
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

    REQUIRE(cupuacu::actions::saveAs(&state, outputPath.string()));

    REQUIRE(std::filesystem::exists(outputPath));
    REQUIRE(session.currentFile == outputPath.string());
    REQUIRE(session.preservationReferenceFile == outputPath.string());
    REQUIRE(session.currentFileExportSettings.has_value());
    REQUIRE(session.preservationReferenceExportSettings.has_value());
    REQUIRE(session.currentFileExportSettings->majorFormat == SF_FORMAT_WAV);
    REQUIRE(session.currentFileExportSettings->subtype == SF_FORMAT_PCM_16);
    REQUIRE(session.overwritePreservation.available);
    REQUIRE(session.overwritePreservation.reason.empty());
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

TEST_CASE("Background save as writes a WAV file and finalizes state", "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-background-save-as"));
    const auto outputPath = cleanup.path() / "exports" / "saved.wav";

    cupuacu::test::StateWithTestPaths state{cleanup.path()};

    auto &session = state.getActiveDocumentSession();
    session.document.initialize(cupuacu::SampleFormat::PCM_S16, 22050, 1, 4);
    session.document.setSample(0, 0, -1.0f, false);
    session.document.setSample(0, 1, -0.25f, false);
    session.document.setSample(0, 2, 0.25f, false);
    session.document.setSample(0, 3, 1.0f, false);

    const auto settings = cupuacu::file::defaultExportSettingsForPath(
        outputPath, session.document.getSampleFormat());
    REQUIRE(settings.has_value());

    REQUIRE(cupuacu::actions::io::queueSaveAs(&state, outputPath.string(),
                                              *settings));
    REQUIRE(state.backgroundSaveJob != nullptr);
    REQUIRE(state.longTask.active);

    drainPendingSaveWork(&state);

    REQUIRE(state.backgroundSaveJob == nullptr);
    REQUIRE_FALSE(state.longTask.active);
    REQUIRE(std::filesystem::exists(outputPath));
    REQUIRE(session.currentFile == outputPath.string());
    REQUIRE(session.preservationReferenceFile == outputPath.string());
    REQUIRE(state.recentFiles == std::vector<std::string>{outputPath.string()});

    int sampleRate = 0;
    int channels = 0;
    const auto frames = readFramesAsFloat(outputPath, sampleRate, channels);
    REQUIRE(sampleRate == 22050);
    REQUIRE(channels == 1);
    REQUIRE(frames.size() == 4);
    REQUIRE(frames[0] == Catch::Approx(-1.0f).margin(1.0f / 32767.0f));
    REQUIRE(frames[1] == Catch::Approx(-0.25f).margin(1.0f / 32767.0f));
    REQUIRE(frames[2] == Catch::Approx(0.25f).margin(1.0f / 32767.0f));
    REQUIRE(frames[3] == Catch::Approx(1.0f).margin(1.0f / 32767.0f));
}

TEST_CASE("Background preserving save as writes against the reference and finalizes state",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-background-preserving-save-as"));
    const auto sourcePath = cleanup.path() / "source.wav";
    const auto outputPath = cleanup.path() / "exports" / "copy.wav";

    writePcm16WavFile(sourcePath, 44100, 1, {100, 200, 300, 400},
                      {'p', 'r', 'e', '!'}, {'p', 'o', 's', 't'});
    const auto originalOrder = readChunkOrder(sourcePath);
    const auto originalJunkChunk = readChunkBytes(sourcePath, "JUNK");
    const auto originalListChunk = readChunkBytes(sourcePath, "LIST");

    cupuacu::test::StateWithTestPaths state{cleanup.path()};
    auto &session = state.getActiveDocumentSession();
    session.currentFile = sourcePath.string();
    cupuacu::file::loadSampleData(&state);
    session.document.setSample(0, 1, 0.25f);

    const auto settings = cupuacu::file::defaultExportSettingsForPath(
        outputPath, session.document.getSampleFormat());
    REQUIRE(settings.has_value());

    REQUIRE(cupuacu::actions::io::queueSaveAsPreserving(
        &state, outputPath.string(), *settings));
    REQUIRE(state.backgroundSaveJob != nullptr);
    REQUIRE(state.longTask.active);

    drainPendingSaveWork(&state);

    REQUIRE(state.backgroundSaveJob == nullptr);
    REQUIRE_FALSE(state.longTask.active);
    REQUIRE(std::filesystem::exists(outputPath));
    REQUIRE(session.currentFile == outputPath.string());
    REQUIRE(session.preservationReferenceFile == outputPath.string());
    REQUIRE(session.currentFileExportSettings.has_value());
    REQUIRE(session.preservationReferenceExportSettings.has_value());
    REQUIRE(readChunkOrder(outputPath) == originalOrder);
    REQUIRE(readChunkBytes(outputPath, "JUNK") == originalJunkChunk);
    REQUIRE(readChunkBytes(outputPath, "LIST") == originalListChunk);

    int sampleRate = 0;
    int channels = 0;
    const auto frames = readFramesAsFloat(outputPath, sampleRate, channels);
    REQUIRE(sampleRate == 44100);
    REQUIRE(channels == 1);
    REQUIRE(frames.size() == 4);
    REQUIRE(frames[0] == Catch::Approx(100.0f / 32767.0f).margin(0.0001f));
    REQUIRE(frames[1] == Catch::Approx(0.25f).margin(1.0f / 32767.0f));
    REQUIRE(frames[2] == Catch::Approx(300.0f / 32767.0f).margin(0.0001f));
    REQUIRE(frames[3] == Catch::Approx(400.0f / 32767.0f).margin(0.0001f));
}

TEST_CASE("Preserving WAV write reports progress while encoding samples",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-preserving-progress"));
    const auto sourcePath = cleanup.path() / "source.wav";
    const auto outputPath = cleanup.path() / "copy.wav";

    std::vector<int16_t> samples(20000, 100);
    writePcm16WavFile(sourcePath, 44100, 1, samples);

    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();
    session.currentFile = sourcePath.string();
    cupuacu::file::loadSampleData(&state);
    session.document.setSample(0, 100, 0.5f);

    const auto settings = cupuacu::file::defaultExportSettingsForPath(
        outputPath, session.document.getSampleFormat());
    REQUIRE(settings.has_value());

    std::vector<double> progressValues;
    const auto lease = session.document.acquireReadLease();
    cupuacu::file::writePreservingFile(cupuacu::file::PreservationWriteInput{
        .document = lease,
        .referencePath = sourcePath,
        .outputPath = outputPath,
        .settings = *settings,
        .progress =
            [&](const std::string &, const std::optional<double> progress)
        {
            if (progress.has_value())
            {
                progressValues.push_back(*progress);
            }
        },
    });

    REQUIRE(std::filesystem::exists(outputPath));
    REQUIRE(progressValues.size() >= 4);
    REQUIRE(progressValues.front() == Catch::Approx(0.0));
    REQUIRE(progressValues.back() == Catch::Approx(1.0));
    REQUIRE(std::is_sorted(progressValues.begin(), progressValues.end()));
    REQUIRE(std::any_of(progressValues.begin(), progressValues.end(),
                        [](const double progress)
                        { return progress > 0.0 && progress < 1.0; }));
}

TEST_CASE("Save as writes native WAV markers", "[file]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-wav-save-markers"));
    const auto wavPath = cleanup.path() / "saved_markers.wav";

    cupuacu::test::StateWithTestPaths state{};
    auto &document = state.getActiveDocumentSession().document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 4);
    document.setSample(0, 0, 0.1f, false);
    document.setSample(0, 1, 0.2f, false);
    document.setSample(0, 2, 0.3f, false);
    document.setSample(0, 3, 0.4f, false);
    document.addMarker(1, "Attack");
    document.addMarker(3, "Tail");

    REQUIRE(cupuacu::actions::saveAs(&state, wavPath.string()));

    const auto markers = cupuacu::file::wav::markers::readMarkers(wavPath);
    REQUIRE(markers.size() == 2);
    REQUIRE(markers[0].frame == 1);
    REQUIRE(markers[0].label == "Attack");
    REQUIRE(markers[1].frame == 3);
    REQUIRE(markers[1].label == "Tail");
}

TEST_CASE("Preserving overwrite updates WAV markers after trim", "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-wav-overwrite-markers"));
    const auto wavPath = cleanup.path() / "overwrite_markers.wav";
    writePcm16WavFile(wavPath, 44100, 1, {1000, 2000, 3000, 4000, 5000, 6000});
    cupuacu::file::wav::markers::rewriteFileWithMarkers(
        wavPath, {{.id = 21, .frame = 1, .label = "A"},
                  {.id = 22, .frame = 4, .label = "B"}});

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().setCurrentFile(wavPath.string());
    cupuacu::file::loadSampleData(&state);
    state.getActiveDocumentSession().selection.setHighest(
        static_cast<double>(state.getActiveDocumentSession().document.getFrameCount()));
    state.getActiveDocumentSession().selection.setValue1(1.0);
    state.getActiveDocumentSession().selection.setValue2(5.0);
    state.addAndDoUndoable(
        std::make_shared<cupuacu::actions::audio::Trim>(&state, 1, 4));
    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto markers = cupuacu::file::wav::markers::readMarkers(wavPath);
    REQUIRE(markers.size() == 2);
    REQUIRE(markers[0].frame == 0);
    REQUIRE(markers[0].label == "A");
    REQUIRE(markers[1].frame == 3);
    REQUIRE(markers[1].label == "B");
}

TEST_CASE("Preserving save as writes against the reference and updates it",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-preserving-save-as"));
    const auto sourcePath = cleanup.path() / "source.wav";
    const auto outputPath = cleanup.path() / "copy.wav";

    writePcm16WavFile(sourcePath, 44100, 1, {100, 200, 300, 400},
                      {'p', 'r', 'e', '!'}, {'p', 'o', 's', 't'});
    const auto originalOrder = readChunkOrder(sourcePath);
    const auto originalJunkChunk = readChunkBytes(sourcePath, "JUNK");
    const auto originalListChunk = readChunkBytes(sourcePath, "LIST");

    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();
    session.currentFile = sourcePath.string();
    cupuacu::file::loadSampleData(&state);
    session.document.setSample(0, 1, 0.25f, false);

    const auto settings = cupuacu::file::defaultExportSettingsForPath(
        outputPath, session.document.getSampleFormat());
    REQUIRE(settings.has_value());

    REQUIRE(cupuacu::actions::saveAsPreserving(&state, outputPath.string(),
                                               *settings));
    REQUIRE(std::filesystem::exists(outputPath));
    REQUIRE(session.currentFile == outputPath.string());
    REQUIRE(session.preservationReferenceFile == outputPath.string());
    REQUIRE(session.currentFileExportSettings.has_value());
    REQUIRE(session.preservationReferenceExportSettings.has_value());
    REQUIRE(readChunkOrder(outputPath) == originalOrder);
    REQUIRE(readChunkBytes(outputPath, "JUNK") == originalJunkChunk);
    REQUIRE(readChunkBytes(outputPath, "LIST") == originalListChunk);
}

TEST_CASE("Preserving save as keeps untouched WAV byte-identical", "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-preserving-save-as-untouched"));
    const auto sourcePath = cleanup.path() / "source.wav";
    const auto outputPath = cleanup.path() / "copy.wav";

    writePcm16WavFile(sourcePath, 44100, 2,
                      {100, -100, 200, -200, 300, -300, 400, -400},
                      {'p', 'r', 'e', '!'}, {'p', 'o', 's', 't'});
    const auto originalBytes = readBytes(sourcePath);

    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();
    session.currentFile = sourcePath.string();
    cupuacu::file::loadSampleData(&state);

    const auto settings = cupuacu::file::defaultExportSettingsForPath(
        outputPath, session.document.getSampleFormat());
    REQUIRE(settings.has_value());

    REQUIRE(cupuacu::actions::saveAsPreserving(&state, outputPath.string(),
                                               *settings));
    REQUIRE(readBytes(outputPath) == originalBytes);
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

    REQUIRE(cupuacu::actions::saveAs(&state, requestedPath.string(), settings));

    REQUIRE_FALSE(std::filesystem::exists(requestedPath));
    REQUIRE(std::filesystem::exists(normalizedPath));
    REQUIRE(session.currentFile == normalizedPath.string());
    REQUIRE(session.currentFileExportSettings.has_value());
    REQUIRE(session.currentFileExportSettings->majorFormat == SF_FORMAT_WAV);
}

TEST_CASE("Save as reports failure without mutating session state", "[file]")
{
    cupuacu::test::ScopedSdlLogSilencer silenceLogs;
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-save-as-failure"));
    const auto blockedParent = cleanup.path() / "blocked";
    const auto requestedPath = blockedParent / "saved.wav";

    std::ofstream blocker(blockedParent, std::ios::binary | std::ios::trunc);
    REQUIRE(blocker.good());
    blocker.close();

    cupuacu::test::StateWithTestPaths state{cleanup.path()};
    auto &session = state.getActiveDocumentSession();
    session.document.initialize(cupuacu::SampleFormat::PCM_S16, 22050, 1, 1);
    session.document.setSample(0, 0, 0.25f, false);
    session.currentFile = "existing.wav";

    std::string reportedTitle;
    std::string reportedMessage;
    state.errorReporter =
        [&](const std::string &title, const std::string &message)
    {
        reportedTitle = title;
        reportedMessage = message;
    };

    REQUIRE_FALSE(cupuacu::actions::saveAs(&state, requestedPath.string()));
    REQUIRE(session.currentFile == "existing.wav");
    REQUIRE_FALSE(session.currentFileExportSettings.has_value());
    REQUIRE(state.recentFiles.empty());
    REQUIRE(reportedTitle == "Save failed");
    REQUIRE(reportedMessage.find(requestedPath.string()) != std::string::npos);
    REQUIRE_FALSE(std::filesystem::exists(requestedPath));
}

TEST_CASE("Save as warns before lossy marker persistence and can be canceled",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-save-warning-lossy-cancel"));
    const auto outputPath = cleanup.path() / "lossy_markers.aiff";

    cupuacu::test::StateWithTestPaths state{};
    auto &document = state.getActiveDocumentSession().document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 4);
    document.setSample(0, 0, 0.1f, false);
    document.addMarker(1, std::string(300, 'x'));

    std::string promptTitle;
    std::string promptMessage;
    state.confirmationReporter =
        [&](const std::string &title, const std::string &message)
    {
        promptTitle = title;
        promptMessage = message;
        return false;
    };

    const auto settings = cupuacu::file::defaultExportSettingsForPath(
        outputPath, document.getSampleFormat());
    REQUIRE(settings.has_value());
    REQUIRE_FALSE(cupuacu::actions::saveAs(&state, outputPath.string(), *settings));
    REQUIRE(promptTitle == "Marker save warning");
    REQUIRE(promptMessage.find("may be truncated") != std::string::npos);
    REQUIRE_FALSE(std::filesystem::exists(outputPath));
}

TEST_CASE("Save as warns before unsupported marker persistence and can be canceled",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-save-warning-unsupported-cancel"));
    const auto outputPath = cleanup.path() / "unsupported_markers.flac";

    cupuacu::test::StateWithTestPaths state{};
    auto &document = state.getActiveDocumentSession().document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 4);
    document.setSample(0, 0, 0.1f, false);
    document.addMarker(1, "Kick");

    std::string promptTitle;
    std::string promptMessage;
    state.confirmationReporter =
        [&](const std::string &title, const std::string &message)
    {
        promptTitle = title;
        promptMessage = message;
        return false;
    };

    const auto settings = cupuacu::file::defaultExportSettingsForPath(
        outputPath, document.getSampleFormat());
    REQUIRE(settings.has_value());
    REQUIRE_FALSE(cupuacu::actions::saveAs(&state, outputPath.string(), *settings));
    REQUIRE(promptTitle == "Marker save warning");
    REQUIRE(promptMessage.find("will not store markers in the audio file") !=
            std::string::npos);
    REQUIRE_FALSE(std::filesystem::exists(outputPath));
}

TEST_CASE("Save as proceeds after accepting lossy marker warning", "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-save-warning-lossy-accept"));
    const auto outputPath = cleanup.path() / "lossy_markers_accept.aiff";

    cupuacu::test::StateWithTestPaths state{};
    auto &document = state.getActiveDocumentSession().document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 4);
    document.setSample(0, 0, 0.1f, false);
    document.setSample(0, 1, 0.2f, false);
    document.addMarker(1, std::string(300, 'x'));

    int promptCount = 0;
    state.confirmationReporter =
        [&](const std::string &, const std::string &)
    {
        ++promptCount;
        return true;
    };

    const auto settings = cupuacu::file::defaultExportSettingsForPath(
        outputPath, document.getSampleFormat());
    REQUIRE(settings.has_value());
    REQUIRE(cupuacu::actions::saveAs(&state, outputPath.string(), *settings));
    REQUIRE(promptCount == 1);
    REQUIRE(std::filesystem::exists(outputPath));
}

TEST_CASE("Overwrite reports failure instead of throwing on invalid target",
          "[file]")
{
    cupuacu::test::ScopedSdlLogSilencer silenceLogs;
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-overwrite-failure"));
    const auto invalidTarget = cleanup.path() / "not-a-file";
    std::filesystem::create_directories(invalidTarget);

    cupuacu::test::StateWithTestPaths state{cleanup.path()};
    auto &session = state.getActiveDocumentSession();
    session.currentFile = invalidTarget.string();
    session.currentFileExportSettings = cupuacu::file::AudioExportSettings{
        .container = cupuacu::file::AudioExportContainer::WAV,
        .codec = cupuacu::file::AudioExportCodec::PCM,
        .majorFormat = SF_FORMAT_WAV,
        .subtype = SF_FORMAT_PCM_16,
        .containerLabel = "WAV",
        .codecLabel = "PCM",
        .encodingLabel = "16-bit PCM",
        .extension = "wav",
    };
    session.document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 2);
    session.document.setSample(0, 0, 0.0f, false);
    session.document.setSample(0, 1, 0.5f, false);

    std::string reportedMessage;
    state.errorReporter =
        [&](const std::string &, const std::string &message)
    {
        reportedMessage = message;
    };

    REQUIRE_FALSE(cupuacu::actions::overwritePreserving(&state));
    REQUIRE(reportedMessage.find(invalidTarget.string()) != std::string::npos);
    REQUIRE(std::filesystem::is_directory(invalidTarget));
}

TEST_CASE("Overwrite warns before unsupported marker persistence and can be canceled",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-overwrite-warning-unsupported-cancel"));
    const auto outputPath = cleanup.path() / "unsupported_overwrite.flac";

    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();
    auto &document = session.document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 4);
    document.setSample(0, 0, 0.1f, false);
    document.addMarker(1, "Kick");
    session.currentFile = outputPath.string();
    session.currentFileExportSettings = cupuacu::file::AudioExportSettings{
        .container = cupuacu::file::AudioExportContainer::FLAC,
        .codec = cupuacu::file::AudioExportCodec::FLAC,
        .majorFormat = SF_FORMAT_FLAC,
        .subtype = SF_FORMAT_PCM_16,
        .containerLabel = "FLAC",
        .codecLabel = "FLAC",
        .encodingLabel = "16-bit FLAC",
        .extension = "flac",
    };

    std::string promptTitle;
    std::string promptMessage;
    state.confirmationReporter =
        [&](const std::string &title, const std::string &message)
    {
        promptTitle = title;
        promptMessage = message;
        return false;
    };

    REQUIRE_FALSE(cupuacu::actions::overwrite(&state));
    REQUIRE(promptTitle == "Marker save warning");
    REQUIRE(promptMessage.find("will not store markers in the audio file") !=
            std::string::npos);
    REQUIRE_FALSE(std::filesystem::exists(outputPath));
}

TEST_CASE("Preserving overwrite warns before lossy marker persistence and can be canceled",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-overwrite-warning-lossy-cancel"));
    const auto aiffPath = cleanup.path() / "lossy_overwrite.aiff";

    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();
    auto &document = session.document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 4);
    document.setSample(0, 0, 0.1f, false);
    document.setSample(0, 1, 0.2f, false);
    document.setSample(0, 2, 0.3f, false);
    document.setSample(0, 3, 0.4f, false);
    const auto settings = cupuacu::file::AudioExportSettings{
        .container = cupuacu::file::AudioExportContainer::AIFF,
        .codec = cupuacu::file::AudioExportCodec::PCM,
        .majorFormat = SF_FORMAT_AIFF,
        .subtype = SF_FORMAT_PCM_16,
        .containerLabel = "AIFF",
        .codecLabel = "PCM",
        .encodingLabel = "16-bit PCM",
        .extension = "aiff",
    };
    REQUIRE(cupuacu::actions::saveAs(&state, aiffPath.string(), settings));

    cupuacu::file::loadSampleData(&state);
    session.document.addMarker(1, std::string(300, 'x'));

    std::string promptTitle;
    std::string promptMessage;
    state.confirmationReporter =
        [&](const std::string &title, const std::string &message)
    {
        promptTitle = title;
        promptMessage = message;
        return false;
    };

    REQUIRE_FALSE(cupuacu::actions::overwritePreserving(&state));
    REQUIRE(promptTitle == "Marker save warning");
    REQUIRE(promptMessage.find("may be truncated") != std::string::npos);
}

TEST_CASE("Overwrite proceeds after accepting unsupported marker warning",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-overwrite-warning-unsupported-accept"));
    const auto outputBasePath =
        cleanup.path() / "unsupported_overwrite_accept";

    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();
    auto &document = session.document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 4);
    document.setSample(0, 0, 0.1f, false);
    document.addMarker(1, "Kick");
    const auto settings = findWritableUnsupportedMarkerSettings(document);
    if (!settings.has_value())
    {
        SUCCEED("No writable export format with unsupported marker persistence "
                "is available in this build.");
        return;
    }

    const auto outputPath =
        cupuacu::file::normalizeExportPath(outputBasePath, *settings);
    session.currentFile = outputPath.string();
    session.currentFileExportSettings = *settings;

    int promptCount = 0;
    state.confirmationReporter =
        [&](const std::string &, const std::string &)
    {
        ++promptCount;
        return true;
    };

    REQUIRE(cupuacu::actions::overwrite(&state));
    REQUIRE(promptCount == 1);
    REQUIRE(std::filesystem::exists(outputPath));
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

TEST_CASE("Export settings description includes exact marker support for WAV",
          "[file]")
{
    cupuacu::Document document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 16);
    document.addMarker(4, "Kick");

    cupuacu::file::AudioExportSettings settings{
        .container = cupuacu::file::AudioExportContainer::WAV,
        .codec = cupuacu::file::AudioExportCodec::PCM,
        .majorFormat = SF_FORMAT_WAV,
        .subtype = SF_FORMAT_PCM_16,
        .containerLabel = "WAV",
        .codecLabel = "PCM",
        .encodingLabel = "16-bit PCM",
        .extension = "wav",
    };

    const auto description =
        cupuacu::file::describeExportSettings(settings, document);
    REQUIRE(description.find("Markers: native support, exact round-trip.") !=
            std::string::npos);
}

TEST_CASE("Export settings description includes lossy marker warning for AIFF",
          "[file]")
{
    cupuacu::Document document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 16);
    document.addMarker(4, std::string(300, 'x'));

    cupuacu::file::AudioExportSettings settings{
        .container = cupuacu::file::AudioExportContainer::AIFF,
        .codec = cupuacu::file::AudioExportCodec::PCM,
        .majorFormat = SF_FORMAT_AIFF,
        .subtype = SF_FORMAT_PCM_16,
        .containerLabel = "AIFF",
        .codecLabel = "PCM",
        .encodingLabel = "16-bit PCM",
        .extension = "aiff",
    };

    const auto description =
        cupuacu::file::describeExportSettings(settings, document);
    REQUIRE(description.find(
                "Markers: native support, but some marker data may be truncated.") !=
            std::string::npos);
}

TEST_CASE(
    "Export settings description includes unsupported marker fallback warning",
    "[file]")
{
    cupuacu::Document document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 16);
    document.addMarker(4, "Kick");

    cupuacu::file::AudioExportSettings settings{
        .container = cupuacu::file::AudioExportContainer::FLAC,
        .codec = cupuacu::file::AudioExportCodec::FLAC,
        .majorFormat = SF_FORMAT_FLAC,
        .subtype = SF_FORMAT_PCM_16,
        .containerLabel = "FLAC",
        .codecLabel = "FLAC",
        .encodingLabel = "16-bit FLAC",
        .extension = "flac",
    };

    const auto description =
        cupuacu::file::describeExportSettings(settings, document);
    REQUIRE(description.find(
                "Markers: no native support; Cupuacu fallback persistence is needed to keep markers.") !=
            std::string::npos);
}

TEST_CASE("Save write plan carries marker persistence assessment", "[file]")
{
    cupuacu::test::StateWithTestPaths state{};
    auto &document = state.getActiveDocumentSession().document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 16);
    document.addMarker(4, std::string(300, 'x'));
    state.getActiveDocumentSession().currentFile = "/tmp/reference.aiff";
    state.getActiveDocumentSession().currentFileExportSettings =
        cupuacu::file::AudioExportSettings{
            .container = cupuacu::file::AudioExportContainer::AIFF,
            .codec = cupuacu::file::AudioExportCodec::PCM,
            .majorFormat = SF_FORMAT_AIFF,
            .subtype = SF_FORMAT_PCM_16,
            .containerLabel = "AIFF",
            .codecLabel = "PCM",
            .encodingLabel = "16-bit PCM",
            .extension = "aiff",
        };
    state.getActiveDocumentSession().setPreservationReference(
        "/tmp/reference.aiff",
        state.getActiveDocumentSession().currentFileExportSettings);

    const auto plan = cupuacu::file::SaveWritePlanner::planPreservingSaveAs(
        &state, *state.getActiveDocumentSession().currentFileExportSettings);
    REQUIRE(plan.markerPersistence.has_value());
    REQUIRE(plan.markerPersistence->fidelity ==
            cupuacu::file::MarkerPersistenceFidelity::Lossy);
}
