#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestPaths.hpp"
#include "actions/Save.hpp"
#include "actions/audio/EditCommands.hpp"
#include "actions/audio/Trim.hpp"
#include "file/AudioExport.hpp"
#include "file/PreservationBackend.hpp"
#include "file/SaveWritePlan.hpp"
#include "file/SampleQuantization.hpp"
#include "file/SndfilePath.hpp"
#include "file/aiff/AiffPreservationSupport.hpp"
#include "file/aiff/AiffParser.hpp"
#include "file/file_loading.hpp"

#include <sndfile.h>

#include <algorithm>
#include <bit>
#include <array>
#include <chrono>
#include <cmath>
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

    void appendBe16(std::vector<uint8_t> &bytes, const uint16_t value)
    {
        appendByte(bytes, static_cast<uint8_t>((value >> 8) & 0xffu));
        appendByte(bytes, static_cast<uint8_t>(value & 0xffu));
    }

    void appendBe32(std::vector<uint8_t> &bytes, const uint32_t value)
    {
        appendByte(bytes, static_cast<uint8_t>((value >> 24) & 0xffu));
        appendByte(bytes, static_cast<uint8_t>((value >> 16) & 0xffu));
        appendByte(bytes, static_cast<uint8_t>((value >> 8) & 0xffu));
        appendByte(bytes, static_cast<uint8_t>(value & 0xffu));
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
        appendBe32(bytes, static_cast<uint32_t>(payload.size()));
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        if ((payload.size() & 1u) != 0u)
        {
            appendByte(bytes, 0);
        }
    }

    std::array<uint8_t, 10> encodeExtended80(const double value)
    {
        std::array<uint8_t, 10> bytes{};
        if (value == 0.0)
        {
            return bytes;
        }

        int sign = 0;
        double absValue = value;
        if (absValue < 0.0)
        {
            sign = 0x8000;
            absValue = -absValue;
        }

        int exponent = 0;
        double mantissa = std::frexp(absValue, &exponent);
        exponent += 16382;

        mantissa = std::ldexp(mantissa, 32);
        const auto hiMant = static_cast<uint32_t>(std::floor(mantissa));
        mantissa = std::ldexp(mantissa - std::floor(mantissa), 32);
        const auto loMant = static_cast<uint32_t>(std::floor(mantissa));

        const uint16_t encodedExponent =
            static_cast<uint16_t>(sign | exponent);
        bytes[0] = static_cast<uint8_t>((encodedExponent >> 8) & 0xffu);
        bytes[1] = static_cast<uint8_t>(encodedExponent & 0xffu);
        bytes[2] = static_cast<uint8_t>((hiMant >> 24) & 0xffu);
        bytes[3] = static_cast<uint8_t>((hiMant >> 16) & 0xffu);
        bytes[4] = static_cast<uint8_t>((hiMant >> 8) & 0xffu);
        bytes[5] = static_cast<uint8_t>(hiMant & 0xffu);
        bytes[6] = static_cast<uint8_t>((loMant >> 24) & 0xffu);
        bytes[7] = static_cast<uint8_t>((loMant >> 16) & 0xffu);
        bytes[8] = static_cast<uint8_t>((loMant >> 8) & 0xffu);
        bytes[9] = static_cast<uint8_t>(loMant & 0xffu);
        return bytes;
    }

    void writePcm16AiffFile(const std::filesystem::path &path,
                            const int sampleRate, const int channels,
                            const std::vector<int16_t> &interleavedSamples,
                            const std::vector<uint8_t> &preSoundChunk = {},
                            const std::vector<uint8_t> &postSoundChunk = {})
    {
        REQUIRE(channels > 0);
        REQUIRE(interleavedSamples.size() % static_cast<size_t>(channels) == 0);

        std::vector<uint8_t> aiffBytes;
        appendAscii(aiffBytes, "FORM");
        appendBe32(aiffBytes, 0);
        appendAscii(aiffBytes, "AIFF");

        std::vector<uint8_t> commChunk;
        appendBe16(commChunk, static_cast<uint16_t>(channels));
        appendBe32(
            commChunk,
            static_cast<uint32_t>(interleavedSamples.size() /
                                  static_cast<size_t>(channels)));
        appendBe16(commChunk, 16);
        const auto sampleRateBytes =
            encodeExtended80(static_cast<double>(sampleRate));
        commChunk.insert(commChunk.end(), sampleRateBytes.begin(),
                         sampleRateBytes.end());
        appendChunk(aiffBytes, "COMM", commChunk);

        if (!preSoundChunk.empty())
        {
            appendChunk(aiffBytes, "NAME", preSoundChunk);
        }

        std::vector<uint8_t> ssndChunk;
        appendBe32(ssndChunk, 0);
        appendBe32(ssndChunk, 0);
        for (const auto sample : interleavedSamples)
        {
            appendBe16(ssndChunk, static_cast<uint16_t>(sample));
        }
        appendChunk(aiffBytes, "SSND", ssndChunk);

        if (!postSoundChunk.empty())
        {
            appendChunk(aiffBytes, "ANNO", postSoundChunk);
        }

        const uint32_t formSize = static_cast<uint32_t>(aiffBytes.size() - 8);
        aiffBytes[4] = static_cast<uint8_t>((formSize >> 24) & 0xffu);
        aiffBytes[5] = static_cast<uint8_t>((formSize >> 16) & 0xffu);
        aiffBytes[6] = static_cast<uint8_t>((formSize >> 8) & 0xffu);
        aiffBytes[7] = static_cast<uint8_t>(formSize & 0xffu);

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write(reinterpret_cast<const char *>(aiffBytes.data()),
                  static_cast<std::streamsize>(aiffBytes.size()));
        REQUIRE(out.good());
    }

    void writePcm8AiffFile(const std::filesystem::path &path,
                           const int sampleRate, const int channels,
                           const std::vector<int8_t> &interleavedSamples,
                           const std::vector<uint8_t> &preSoundChunk = {},
                           const std::vector<uint8_t> &postSoundChunk = {})
    {
        REQUIRE(channels > 0);
        REQUIRE(interleavedSamples.size() % static_cast<size_t>(channels) == 0);

        std::vector<uint8_t> aiffBytes;
        appendAscii(aiffBytes, "FORM");
        appendBe32(aiffBytes, 0);
        appendAscii(aiffBytes, "AIFF");

        std::vector<uint8_t> commChunk;
        appendBe16(commChunk, static_cast<uint16_t>(channels));
        appendBe32(
            commChunk,
            static_cast<uint32_t>(interleavedSamples.size() /
                                  static_cast<size_t>(channels)));
        appendBe16(commChunk, 8);
        const auto sampleRateBytes =
            encodeExtended80(static_cast<double>(sampleRate));
        commChunk.insert(commChunk.end(), sampleRateBytes.begin(),
                         sampleRateBytes.end());
        appendChunk(aiffBytes, "COMM", commChunk);

        if (!preSoundChunk.empty())
        {
            appendChunk(aiffBytes, "NAME", preSoundChunk);
        }

        std::vector<uint8_t> ssndChunk;
        appendBe32(ssndChunk, 0);
        appendBe32(ssndChunk, 0);
        for (const auto sample : interleavedSamples)
        {
            appendByte(ssndChunk, static_cast<uint8_t>(sample));
        }
        appendChunk(aiffBytes, "SSND", ssndChunk);

        if (!postSoundChunk.empty())
        {
            appendChunk(aiffBytes, "ANNO", postSoundChunk);
        }

        const uint32_t formSize = static_cast<uint32_t>(aiffBytes.size() - 8);
        aiffBytes[4] = static_cast<uint8_t>((formSize >> 24) & 0xffu);
        aiffBytes[5] = static_cast<uint8_t>((formSize >> 16) & 0xffu);
        aiffBytes[6] = static_cast<uint8_t>((formSize >> 8) & 0xffu);
        aiffBytes[7] = static_cast<uint8_t>(formSize & 0xffu);

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write(reinterpret_cast<const char *>(aiffBytes.data()),
                  static_cast<std::streamsize>(aiffBytes.size()));
        REQUIRE(out.good());
    }

    void writeFloat32AifcFile(const std::filesystem::path &path,
                              const int sampleRate, const int channels,
                              const std::vector<float> &interleavedSamples,
                              const std::vector<uint8_t> &preSoundChunk = {},
                              const std::vector<uint8_t> &postSoundChunk = {})
    {
        REQUIRE(channels > 0);
        REQUIRE(interleavedSamples.size() % static_cast<size_t>(channels) == 0);

        std::vector<uint8_t> aiffBytes;
        appendAscii(aiffBytes, "FORM");
        appendBe32(aiffBytes, 0);
        appendAscii(aiffBytes, "AIFC");

        std::vector<uint8_t> fverChunk;
        appendBe32(fverChunk, 0xA2805140u);
        appendChunk(aiffBytes, "FVER", fverChunk);

        std::vector<uint8_t> commChunk;
        appendBe16(commChunk, static_cast<uint16_t>(channels));
        appendBe32(
            commChunk,
            static_cast<uint32_t>(interleavedSamples.size() /
                                  static_cast<size_t>(channels)));
        appendBe16(commChunk, 32);
        const auto sampleRateBytes =
            encodeExtended80(static_cast<double>(sampleRate));
        commChunk.insert(commChunk.end(), sampleRateBytes.begin(),
                         sampleRateBytes.end());
        appendAscii(commChunk, "FL32");
        appendBe16(commChunk, 0);
        appendChunk(aiffBytes, "COMM", commChunk);

        if (!preSoundChunk.empty())
        {
            appendChunk(aiffBytes, "NAME", preSoundChunk);
        }

        std::vector<uint8_t> ssndChunk;
        appendBe32(ssndChunk, 0);
        appendBe32(ssndChunk, 0);
        for (const auto sample : interleavedSamples)
        {
            const auto bits = std::bit_cast<std::uint32_t>(sample);
            appendBe32(ssndChunk, bits);
        }
        appendChunk(aiffBytes, "SSND", ssndChunk);

        if (!postSoundChunk.empty())
        {
            appendChunk(aiffBytes, "ANNO", postSoundChunk);
        }

        const uint32_t formSize = static_cast<uint32_t>(aiffBytes.size() - 8);
        aiffBytes[4] = static_cast<uint8_t>((formSize >> 24) & 0xffu);
        aiffBytes[5] = static_cast<uint8_t>((formSize >> 16) & 0xffu);
        aiffBytes[6] = static_cast<uint8_t>((formSize >> 8) & 0xffu);
        aiffBytes[7] = static_cast<uint8_t>(formSize & 0xffu);

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write(reinterpret_cast<const char *>(aiffBytes.data()),
                  static_cast<std::streamsize>(aiffBytes.size()));
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

    std::vector<uint8_t> sliceBytes(const std::vector<uint8_t> &bytes,
                                    const std::size_t start,
                                    const std::size_t count)
    {
        REQUIRE(start + count <= bytes.size());
        return std::vector<uint8_t>(
            bytes.begin() + static_cast<std::ptrdiff_t>(start),
            bytes.begin() + static_cast<std::ptrdiff_t>(start + count));
    }

    std::vector<uint8_t> readChunkBytes(const std::filesystem::path &path,
                                        const char (&chunkId)[5])
    {
        const auto parsed = cupuacu::file::aiff::AiffParser::parseFile(path);
        const auto *chunk = parsed.findChunk(chunkId);
        REQUIRE(chunk != nullptr);

        return sliceBytes(readBytes(path), chunk->headerOffset,
                          8 + chunk->paddedPayloadSize);
    }

    uint32_t readFormSizeField(const std::filesystem::path &path)
    {
        const auto bytes = readBytes(path);
        REQUIRE(bytes.size() >= 8);
        return (static_cast<uint32_t>(bytes[4]) << 24) |
               (static_cast<uint32_t>(bytes[5]) << 16) |
               (static_cast<uint32_t>(bytes[6]) << 8) |
               static_cast<uint32_t>(bytes[7]);
    }

    uint32_t readCommSampleFrameCount(const std::filesystem::path &path)
    {
        const auto parsed = cupuacu::file::aiff::AiffParser::parseFile(path);
        REQUIRE(parsed.commSampleFrameCountOffset > 0);
        const auto bytes = readBytes(path);
        const auto offset = parsed.commSampleFrameCountOffset;
        return (static_cast<uint32_t>(bytes[offset]) << 24) |
               (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
               (static_cast<uint32_t>(bytes[offset + 2]) << 8) |
               static_cast<uint32_t>(bytes[offset + 3]);
    }

    std::vector<std::string> readChunkOrder(const std::filesystem::path &path)
    {
        const auto parsed = cupuacu::file::aiff::AiffParser::parseFile(path);
        std::vector<std::string> order;
        order.reserve(parsed.chunks.size());
        for (const auto &chunk : parsed.chunks)
        {
            order.emplace_back(chunk.id, chunk.id + 4);
        }
        return order;
    }
} // namespace

TEST_CASE("AIFF preservation support reports supported for valid PCM16 overwrite",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-aiff-preservation-support-ok"));
    const auto aiffPath = cleanup.path() / "preserve_ok.aiff";

    writePcm16AiffFile(aiffPath, 44100, 1, {100, 200, 300, 400});

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = aiffPath.string();
    cupuacu::file::loadSampleData(&state);

    const auto support =
        cupuacu::file::aiff::AiffPreservationSupport::assessOverwrite(&state);
    REQUIRE(support.supported);
    REQUIRE(support.reason.empty());
}

TEST_CASE("AIFF preservation support reports supported for valid PCM8 overwrite",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-aiff-preservation-support-pcm8"));
    const auto aiffPath = cleanup.path() / "preserve_ok_pcm8.aiff";

    writePcm8AiffFile(aiffPath, 44100, 1, {10, 20, 30, 40});

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = aiffPath.string();
    cupuacu::file::loadSampleData(&state);

    const auto support =
        cupuacu::file::aiff::AiffPreservationSupport::assessOverwrite(&state);
    REQUIRE(support.supported);
    REQUIRE(support.reason.empty());
}

TEST_CASE("AIFF preservation support reports supported for valid float32 overwrite",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-aifc-preservation-support-f32"));
    const auto aiffPath = cleanup.path() / "preserve_ok_f32.aiff";

    writeFloat32AifcFile(aiffPath, 44100, 1, {0.0f, 0.25f, -0.25f, 0.5f});

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = aiffPath.string();
    cupuacu::file::loadSampleData(&state);

    const auto support =
        cupuacu::file::aiff::AiffPreservationSupport::assessOverwrite(&state);
    REQUIRE(support.supported);
    REQUIRE(support.reason.empty());
}

TEST_CASE("Overwrite keeps untouched 16-bit PCM AIFF byte-identical", "[file]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-aiff-overwrite"));
    const auto aiffPath = cleanup.path() / "untouched.aiff";

    writePcm16AiffFile(aiffPath, 44100, 2,
                       {0, 1200, -1200, 32000, -32000, 42, -42, 8192});
    const auto originalBytes = readBytes(aiffPath);

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = aiffPath.string();
    cupuacu::file::loadSampleData(&state);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto rewrittenBytes = readBytes(aiffPath);
    REQUIRE(rewrittenBytes == originalBytes);
}

TEST_CASE("Overwrite preserves non-audio AIFF chunks around SSND", "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-aiff-preserve-chunks"));
    const auto aiffPath = cleanup.path() / "preserve_chunks.aiff";

    writePcm16AiffFile(aiffPath, 48000, 1, {100, -100, 200, -200},
                       {'a', 'b', 'c'}, {'x', 'y', 'z', 'q'});
    const auto originalNameChunk = readChunkBytes(aiffPath, "NAME");
    const auto originalAnnoChunk = readChunkBytes(aiffPath, "ANNO");

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = aiffPath.string();
    cupuacu::file::loadSampleData(&state);
    state.getActiveDocumentSession().document.setSample(0, 1, 0.25f);
    state.getActiveDocumentSession().document.updateWaveformCache();
    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    REQUIRE(readChunkBytes(aiffPath, "NAME") == originalNameChunk);
    REQUIRE(readChunkBytes(aiffPath, "ANNO") == originalAnnoChunk);
}

TEST_CASE("Overwrite after length change keeps AIFF sizes and chunk order consistent",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-aiff-length-change"));
    const auto aiffPath = cleanup.path() / "length_change.aiff";

    writePcm16AiffFile(aiffPath, 44100, 1, {100, 200, 300, 400},
                       {'n', 'a', 'm', 'e'}, {'t', 'a', 'i', 'l'});
    const auto originalOrder = readChunkOrder(aiffPath);

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = aiffPath.string();
    cupuacu::file::loadSampleData(&state);

    auto &doc = state.getActiveDocumentSession().document;
    doc.insertFrames(4, 1);
    doc.setSample(0, 4, 0.5f, true);
    doc.updateWaveformCache();

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    int sampleRate = 0;
    int channels = 0;
    const auto frames = readFramesAsFloat(aiffPath, sampleRate, channels);
    REQUIRE(sampleRate == 44100);
    REQUIRE(channels == 1);
    REQUIRE(frames.size() == 5);
    REQUIRE(readCommSampleFrameCount(aiffPath) == 5);
    REQUIRE(readFormSizeField(aiffPath) + 8 == readBytes(aiffPath).size());
    REQUIRE(readChunkOrder(aiffPath) == originalOrder);
}

TEST_CASE("Overwrite patches only one mono PCM16 AIFF sample in place", "[file]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-aiff-mono-patch"));
    const auto aiffPath = cleanup.path() / "mono.aiff";

    writePcm16AiffFile(aiffPath, 44100, 1, {0, 1000, -1000, 2000});
    const auto originalBytes = readBytes(aiffPath);

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = aiffPath.string();
    cupuacu::file::loadSampleData(&state);
    state.getActiveDocumentSession().document.setSample(0, 2, 0.25f);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto updatedBytes = readBytes(aiffPath);
    const auto differingOffsets =
        findDifferingByteOffsets(originalBytes, updatedBytes);
    REQUIRE(differingOffsets.size() == sizeof(std::int16_t));
}

TEST_CASE("Overwrite patches only one mono PCM8 AIFF sample in place", "[file]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-aiff-mono-patch-8"));
    const auto aiffPath = cleanup.path() / "mono8.aiff";

    writePcm8AiffFile(aiffPath, 44100, 1, {0, 10, -10, 20});
    const auto originalBytes = readBytes(aiffPath);

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = aiffPath.string();
    cupuacu::file::loadSampleData(&state);
    state.getActiveDocumentSession().document.setSample(0, 2, 0.25f);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto updatedBytes = readBytes(aiffPath);
    const auto differingOffsets =
        findDifferingByteOffsets(originalBytes, updatedBytes);
    REQUIRE(differingOffsets.size() == 1);
}

TEST_CASE("Overwrite patches only one mono float32 AIFF sample in place",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-aiff-mono-patch-f32"));
    const auto aiffPath = cleanup.path() / "mono_f32.aiff";

    writeFloat32AifcFile(aiffPath, 44100, 1, {0.0f, 0.25f, -0.25f, 0.5f});
    const auto originalBytes = readBytes(aiffPath);

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = aiffPath.string();
    cupuacu::file::loadSampleData(&state);
    state.getActiveDocumentSession().document.setSample(0, 2, 0.75f);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto updatedBytes = readBytes(aiffPath);
    const auto differingOffsets =
        findDifferingByteOffsets(originalBytes, updatedBytes);
    REQUIRE_FALSE(differingOffsets.empty());
    REQUIRE(differingOffsets.size() <= sizeof(float));

    const auto parsed = cupuacu::file::aiff::AiffParser::parseFile(aiffPath);
    const auto expectedStart = parsed.soundDataOffset + sizeof(float) * 2;
    const auto expectedEnd = expectedStart + sizeof(float);
    for (const auto offset : differingOffsets)
    {
        REQUIRE(offset >= expectedStart);
        REQUIRE(offset < expectedEnd);
    }
}

TEST_CASE("Overwrite patches only one stereo channel AIFF sample in place",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-aiff-stereo-channel-patch"));
    const auto aiffPath = cleanup.path() / "stereo.aiff";

    writePcm16AiffFile(aiffPath, 44100, 2, {10, 20, 30, 40, 50, 60});
    const auto originalBytes = readBytes(aiffPath);

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = aiffPath.string();
    cupuacu::file::loadSampleData(&state);
    state.getActiveDocumentSession().document.setSample(1, 1, -0.5f);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto updatedBytes = readBytes(aiffPath);
    const auto differingOffsets =
        findDifferingByteOffsets(originalBytes, updatedBytes);
    REQUIRE(differingOffsets.size() == sizeof(std::int16_t));
}

TEST_CASE("Overwrite after trim preserves surviving PCM16 AIFF sample bytes",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-aiff-trim-preserve"));
    const auto aiffPath = cleanup.path() / "trim.aiff";

    writePcm16AiffFile(aiffPath, 44100, 1, {100, 200, 300, 400, 500},
                       {'p', 'r', 'e', '!'}, {'p', 'o', 's', 't'});
    const auto originalSsndChunk = readChunkBytes(aiffPath, "SSND");

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = aiffPath.string();
    cupuacu::file::loadSampleData(&state);
    state.addAndDoUndoable(
        std::make_shared<cupuacu::actions::audio::Trim>(&state, 1, 3));

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto updatedSsndChunk = readChunkBytes(aiffPath, "SSND");
    REQUIRE(sliceBytes(updatedSsndChunk, 16, 6) ==
            sliceBytes(originalSsndChunk, 18, 6));
    REQUIRE(readChunkBytes(aiffPath, "NAME") ==
            std::vector<uint8_t>({'N', 'A', 'M', 'E', 0, 0, 0, 4, 'p', 'r', 'e',
                                  '!'}));
    REQUIRE(readChunkBytes(aiffPath, "ANNO") ==
            std::vector<uint8_t>({'A', 'N', 'N', 'O', 0, 0, 0, 4, 'p', 'o', 's',
                                  't'}));
}

TEST_CASE("Overwrite after cut preserves surviving PCM16 AIFF sample bytes",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-aiff-cut-preserve"));
    const auto aiffPath = cleanup.path() / "cut.aiff";

    writePcm16AiffFile(aiffPath, 44100, 1, {100, 200, 300, 400, 500, 600},
                       {'p', 'r', 'e', '!'}, {'p', 'o', 's', 't'});
    const auto originalSsndChunk = readChunkBytes(aiffPath, "SSND");

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = aiffPath.string();
    cupuacu::file::loadSampleData(&state);
    auto &session = state.getActiveDocumentSession();
    session.selection.setValue1(2.0);
    session.selection.setValue2(4.0);
    cupuacu::actions::audio::performCut(&state);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto updatedSsndChunk = readChunkBytes(aiffPath, "SSND");
    std::vector<uint8_t> expectedAudio;
    expectedAudio.insert(expectedAudio.end(),
                         originalSsndChunk.begin() + 16,
                         originalSsndChunk.begin() + 20);
    expectedAudio.insert(expectedAudio.end(),
                         originalSsndChunk.begin() + 24,
                         originalSsndChunk.begin() + 28);
    REQUIRE(sliceBytes(updatedSsndChunk, 16, expectedAudio.size()) ==
            expectedAudio);
}

TEST_CASE("Overwrite after insert silence preserves surrounding PCM16 AIFF sample bytes",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-aiff-insert-silence"));
    const auto aiffPath = cleanup.path() / "insert_silence.aiff";

    writePcm16AiffFile(aiffPath, 44100, 1, {100, 200, 300, 400},
                       {'p', 'r', 'e', '!'}, {'p', 'o', 's', 't'});
    const auto originalSsndChunk = readChunkBytes(aiffPath, "SSND");

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = aiffPath.string();
    cupuacu::file::loadSampleData(&state);
    auto &session = state.getActiveDocumentSession();
    session.cursor = 2;
    cupuacu::actions::audio::performInsertSilence(&state, 2);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto updatedSsndChunk = readChunkBytes(aiffPath, "SSND");
    std::vector<uint8_t> expectedAudio;
    expectedAudio.insert(expectedAudio.end(),
                         originalSsndChunk.begin() + 16,
                         originalSsndChunk.begin() + 20);
    expectedAudio.insert(expectedAudio.end(), 4, 0);
    expectedAudio.insert(expectedAudio.end(),
                         originalSsndChunk.begin() + 20,
                         originalSsndChunk.begin() + 24);
    REQUIRE(sliceBytes(updatedSsndChunk, 16, expectedAudio.size()) ==
            expectedAudio);
}

TEST_CASE("Overwrite after trim preserves dirty and clean AIFF survivors distinctly",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-aiff-trim-dirty-survivor"));
    const auto aiffPath = cleanup.path() / "trim_dirty.aiff";

    writePcm16AiffFile(aiffPath, 44100, 1, {100, 200, 300, 400, 500},
                       {'p', 'r', 'e', '!'}, {'p', 'o', 's', 't'});
    const auto originalSsndChunk = readChunkBytes(aiffPath, "SSND");

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = aiffPath.string();
    cupuacu::file::loadSampleData(&state);
    auto &document = state.getActiveDocumentSession().document;
    document.setSample(0, 2, 0.25f);
    state.addAndDoUndoable(
        std::make_shared<cupuacu::actions::audio::Trim>(&state, 1, 3));

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto updatedSsndChunk = readChunkBytes(aiffPath, "SSND");
    const auto dirtySample = static_cast<std::int16_t>(
        cupuacu::file::quantizeIntegerPcmSample(cupuacu::SampleFormat::PCM_S16,
                                                0.25f, false));
    std::vector<uint8_t> expectedAudio;
    expectedAudio.insert(expectedAudio.end(),
                         originalSsndChunk.begin() + 18,
                         originalSsndChunk.begin() + 20);
    expectedAudio.push_back(
        static_cast<uint8_t>((static_cast<uint16_t>(dirtySample) >> 8) & 0xffu));
    expectedAudio.push_back(
        static_cast<uint8_t>(static_cast<uint16_t>(dirtySample) & 0xffu));
    expectedAudio.insert(expectedAudio.end(),
                         originalSsndChunk.begin() + 22,
                         originalSsndChunk.begin() + 24);
    REQUIRE(sliceBytes(updatedSsndChunk, 16, expectedAudio.size()) ==
            expectedAudio);
}

TEST_CASE("Overwrite after stereo cut preserves surviving interleaved AIFF PCM16 sample bytes",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-aiff-stereo-cut-preserve"));
    const auto aiffPath = cleanup.path() / "stereo_cut.aiff";

    writePcm16AiffFile(aiffPath, 44100, 2, {10, 20, 30, 40, 50, 60, 70, 80},
                       {'p', 'r', 'e', '!'}, {'p', 'o', 's', 't'});
    const auto originalSsndChunk = readChunkBytes(aiffPath, "SSND");

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = aiffPath.string();
    cupuacu::file::loadSampleData(&state);
    auto &session = state.getActiveDocumentSession();
    session.selection.setValue1(1.0);
    session.selection.setValue2(3.0);
    cupuacu::actions::audio::performCut(&state);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));

    const auto updatedSsndChunk = readChunkBytes(aiffPath, "SSND");
    std::vector<uint8_t> expectedAudio;
    expectedAudio.insert(expectedAudio.end(),
                         originalSsndChunk.begin() + 16,
                         originalSsndChunk.begin() + 20);
    expectedAudio.insert(expectedAudio.end(),
                         originalSsndChunk.begin() + 28,
                         originalSsndChunk.begin() + 32);
    REQUIRE(sliceBytes(updatedSsndChunk, 16, expectedAudio.size()) ==
            expectedAudio);
}

TEST_CASE("Second AIFF overwrite after edit is byte-identical", "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-aiff-second-overwrite"));
    const auto aiffPath = cleanup.path() / "second.aiff";

    writePcm16AiffFile(aiffPath, 44100, 1, {0, 1000, 2000, 3000});

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = aiffPath.string();
    cupuacu::file::loadSampleData(&state);
    state.getActiveDocumentSession().document.setSample(0, 1, -0.25f);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));
    const auto savedBytes = readBytes(aiffPath);

    REQUIRE(cupuacu::actions::overwritePreserving(&state));
    REQUIRE(readBytes(aiffPath) == savedBytes);
}

TEST_CASE("Save write planner selects preserving AIFF save paths when supported",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-aiff-save-write-plan"));
    const auto aiffPath = cleanup.path() / "plan_preserving.aiff";

    writePcm16AiffFile(aiffPath, 44100, 1, {100, 200, 300, 400});

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = aiffPath.string();
    cupuacu::file::loadSampleData(&state);

    const auto settings = cupuacu::file::defaultExportSettingsForPath(
        aiffPath, state.getActiveDocumentSession().document.getSampleFormat());
    REQUIRE(settings.has_value());
    REQUIRE(cupuacu::file::preservationBackendKindForSettings(*settings) ==
            cupuacu::file::PreservationBackendKind::AiffPcm);

    const auto overwritePlan =
        cupuacu::file::SaveWritePlanner::planPreservingOverwrite(&state, *settings);
    REQUIRE(overwritePlan.mode ==
            cupuacu::file::SaveWriteMode::OverwritePreservingRewrite);

    const auto saveAsPlan =
        cupuacu::file::SaveWritePlanner::planPreservingSaveAs(&state, *settings);
    REQUIRE(saveAsPlan.mode ==
            cupuacu::file::SaveWriteMode::OverwritePreservingRewrite);
}

TEST_CASE("Preservation backend current-file overwrite uses AIFF writer",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-aiff-backend-overwrite-current"));
    const auto aiffPath = cleanup.path() / "backend_overwrite.aiff";

    writePcm16AiffFile(aiffPath, 44100, 1, {100, 200, 300, 400},
                       {'p', 'r', 'e', '!'}, {'p', 'o', 's', 't'});
    const auto originalOrder = readChunkOrder(aiffPath);
    const auto originalNameChunk = readChunkBytes(aiffPath, "NAME");
    const auto originalAnnoChunk = readChunkBytes(aiffPath, "ANNO");

    cupuacu::test::StateWithTestPaths state{};
    auto &session = state.getActiveDocumentSession();
    session.currentFile = aiffPath.string();
    cupuacu::file::loadSampleData(&state);
    session.document.setSample(0, 1, 0.25f);

    const auto settings = cupuacu::file::defaultExportSettingsForPath(
        aiffPath, session.document.getSampleFormat());
    REQUIRE(settings.has_value());

    cupuacu::file::overwritePreservingCurrentFile(&state, *settings);

    REQUIRE(readChunkOrder(aiffPath) == originalOrder);
    REQUIRE(readChunkBytes(aiffPath, "NAME") == originalNameChunk);
    REQUIRE(readChunkBytes(aiffPath, "ANNO") == originalAnnoChunk);

    int sampleRate = 0;
    int channels = 0;
    const auto frames = readFramesAsFloat(aiffPath, sampleRate, channels);
    REQUIRE(sampleRate == 44100);
    REQUIRE(channels == 1);
    REQUIRE(frames.size() == 4);
    REQUIRE(frames[1] == Catch::Approx(0.25f).margin(1.0f / 32767.0f));
}

TEST_CASE("Preserving AIFF save as writes against the reference and updates it",
          "[file]")
{
    ScopedDirCleanup cleanup(
        makeUniqueTempDir("cupuacu-test-aiff-preserving-save-as"));
    const auto sourcePath = cleanup.path() / "source.aiff";
    const auto outputPath = cleanup.path() / "copy.aiff";

    writePcm16AiffFile(sourcePath, 44100, 1, {100, 200, 300, 400},
                       {'p', 'r', 'e', '!'}, {'p', 'o', 's', 't'});
    const auto originalOrder = readChunkOrder(sourcePath);
    const auto originalNameChunk = readChunkBytes(sourcePath, "NAME");
    const auto originalAnnoChunk = readChunkBytes(sourcePath, "ANNO");

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
    REQUIRE(readChunkBytes(outputPath, "NAME") == originalNameChunk);
    REQUIRE(readChunkBytes(outputPath, "ANNO") == originalAnnoChunk);
}
