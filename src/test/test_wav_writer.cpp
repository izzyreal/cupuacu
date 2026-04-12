#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestSdlLogSilencer.hpp"
#include "TestPaths.hpp"
#include "actions/Save.hpp"
#include "actions/audio/RecordEdit.hpp"
#include "actions/audio/EditCommands.hpp"
#include "actions/audio/Trim.hpp"
#include "file/AudioExport.hpp"
#include "file/SampleQuantization.hpp"
#include "file/SndfilePath.hpp"
#include "file/file_loading.hpp"
#include "file/wav/WavParser.hpp"
#include "file/wav/WavPreservationSupport.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/Window.hpp"

#include <sndfile.h>

#include <algorithm>
#include <chrono>
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
    REQUIRE(cupuacu::actions::overwrite(&state));

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
    REQUIRE(cupuacu::actions::overwrite(&state));

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

    REQUIRE(cupuacu::actions::overwrite(&state));

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

    REQUIRE(cupuacu::actions::overwrite(&state));

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

    REQUIRE(cupuacu::actions::overwrite(&state));

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

    REQUIRE(cupuacu::actions::overwrite(&state));

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

    REQUIRE(cupuacu::actions::overwrite(&state));

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

    REQUIRE_FALSE(cupuacu::actions::overwrite(&state));
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

    REQUIRE_FALSE(cupuacu::actions::overwrite(&state));
    REQUIRE(reportedMessage.find("Not a 16-bit PCM WAV file") !=
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

    REQUIRE_FALSE(cupuacu::actions::overwrite(&state));
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

    REQUIRE_FALSE(cupuacu::actions::overwrite(&state));
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

    REQUIRE_FALSE(cupuacu::actions::overwrite(&state));
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

    REQUIRE(cupuacu::actions::overwrite(&state));

    const auto updatedBytes = readBytes(wavPath);
    const auto differingOffsets =
        findDifferingByteOffsets(originalBytes, updatedBytes);
    REQUIRE(differingOffsets.size() == sizeof(std::int16_t));
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

    REQUIRE(cupuacu::actions::overwrite(&state));

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

    REQUIRE(cupuacu::actions::overwrite(&state));

    const auto updatedData = readChunkPayload(wavPath, "data");
    REQUIRE(updatedData ==
            sliceBytes(originalData, sizeof(std::int16_t) * 1,
                       sizeof(std::int16_t) * 3));
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

    REQUIRE(cupuacu::actions::overwrite(&state));

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

    REQUIRE(cupuacu::actions::overwrite(&state));

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

    REQUIRE(cupuacu::actions::overwrite(&state));

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

    REQUIRE(cupuacu::actions::overwrite(&state));

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

    REQUIRE(cupuacu::actions::overwrite(&state));

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

    REQUIRE(cupuacu::actions::overwrite(&state));

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

    REQUIRE(cupuacu::actions::overwrite(&state));

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

    REQUIRE(cupuacu::actions::overwrite(&state));
    const auto savedBytes = readBytes(wavPath);

    REQUIRE(cupuacu::actions::overwrite(&state));
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

    REQUIRE(cupuacu::actions::overwrite(&state));

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
    REQUIRE(session.currentFileExportSettings.has_value());
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

    REQUIRE_FALSE(cupuacu::actions::overwrite(&state));
    REQUIRE(reportedMessage.find(invalidTarget.string()) != std::string::npos);
    REQUIRE(std::filesystem::is_directory(invalidTarget));
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
