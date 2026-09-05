#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "TestPaths.hpp"
#include "file/OwnedAudioImport.hpp"
#include "storage/DocumentAudioReader.hpp"
#include "storage/AsyncAudioReader.hpp"
#include <chrono>

using namespace cupuacu;

TEST_CASE(
    "Disk revisions and slices read unaligned ranges within a shared cache "
    "budget",
    "[owned-audio]")
{
    const auto root = test::makeUniqueTestRoot("audio-blocks") / "store";
    auto store = std::make_shared<storage::AudioBlockStore>(
        root, 2 * storage::AudioBlockBytes);
    auto cache =
        std::make_shared<storage::DecodedBlockCache>(storage::AudioBlockBytes);
    constexpr int64_t frames = 3 * storage::AudioBlockFrames + 17;
    storage::AudioShape shape{frames, 2, 48000, SampleFormat::FLOAT32};
    storage::AudioRevisionBuilder builder(shape, store, cache);
    Document document;
    document.initialize(shape.format, shape.sampleRate, shape.channels, frames);
    std::vector<float> input(5003 * 2);
    for (int64_t start = 0; start < frames; start += 5003)
    {
        const auto count = std::min<int64_t>(5003, frames - start);
        for (int64_t i = 0; i < count; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                input[i * 2 + c] = float((start + i) % 1009 - 500 + c) / 1024;
            }
        }
        builder.appendInterleaved(
            std::span<const float>(input).first(count * 2));
        document.writeInterleavedFloatBlock(start, input.data(), count, 2,
                                            false);
    }
    auto revision = builder.finish();
    storage::DocumentAudioReader reference(document);
    document.setSample(0, 0, 99); // Adapter pins the earlier revision.
    std::vector<float> actual(8193), expected(8193);
    for (const auto start : {int64_t{0}, int64_t{65530},
                             frames - int64_t(actual.size()), int64_t{1}})
    {
        for (int c = 0; c < 2; ++c)
        {
            revision->readChannel(c, start, actual);
            reference.readChannel(c, start, expected);
            REQUIRE(actual == expected);
            REQUIRE(cache->stats().residentBytes <= storage::AudioBlockBytes);
        }
    }
    REQUIRE(cache->stats().misses > 1);
    const auto diskBefore = store->ioBytes().first;
    revision->readChannel(1, 1, actual);
    REQUIRE(store->ioBytes().first == diskBefore);
    REQUIRE_THROWS_AS(revision->readChannel(0, -1, actual), std::out_of_range);
    REQUIRE_THROWS_AS(revision->readChannel(2, 0, actual), std::out_of_range);
    REQUIRE_THROWS_AS(revision->readChannel(0, INT64_MAX, actual),
                      std::out_of_range);
    revision->readChannel(0, frames, {});
    REQUIRE_THROWS(builder.finish());

    {
        storage::AsyncAudioReader async(revision, actual.size());
        const auto generation = async.submit(1, 65530, actual.size());
        std::optional<storage::AsyncAudioReader::Result> result;
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!(result = async.takePublished()) &&
               std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }
        REQUIRE(result);
        REQUIRE(result->generation == generation);
        REQUIRE_FALSE(result->error);
        reference.readChannel(1, 65530, expected);
        REQUIRE(result->samples == expected);
        async.close();
        async.waitUntilClosed();
    }

    auto slice = std::make_unique<storage::AudioSlice>(revision, 65530, 20000);
    slice->readChannel(0, 13, actual);
    reference.readChannel(0, 65543, expected);
    REQUIRE(actual == expected);
    REQUIRE_THROWS_AS(slice->readChannel(0, 19999, actual), std::out_of_range);
    revision.reset();
    store.reset();
    REQUIRE(std::filesystem::exists(root));
    slice.reset();
    REQUIRE_FALSE(std::filesystem::exists(root));
}

TEST_CASE(
    "Sub-block cache budgets use bounded direct reads and report storage "
    "failures",
    "[owned-audio]")
{
    const auto root = test::makeUniqueTestRoot("audio-cache-bypass") / "store";
    storage::AudioBlockStore store(root);
    std::array<float, 17> input{};
    input[16] = 0.25f;
    const auto block = store.append(input);
    store.flush();
    storage::DecodedBlockCache cache(16);
    std::array<float, 1> output{};
    cache.read(store, block, 16, output);
    REQUIRE(output[0] == 0.25f);
    REQUIRE(cache.stats().residentBytes == 0);
    REQUIRE_THROWS_AS(cache.read(store, block, 17, output), std::out_of_range);
    REQUIRE_THROWS_AS(store.read({UINT64_MAX, 0, 1}, 0, output),
                      std::out_of_range);
    std::filesystem::resize_file(root / "samples-0.bin", 0);
    REQUIRE_THROWS(store.read(block, 0, input));
}

TEST_CASE(
    "Owned import preserves source bytes, streams peaks and cleans up canceled "
    "transactions",
    "[owned-audio]")
{
    const file::OwnedImportOptions options{GENERATE(false, true)};
    const auto root = test::makeUniqueTestRoot("owned-import");
    std::filesystem::create_directories(root);
    const auto source = root / "precision.wav";
    SF_INFO info{};
    info.channels = 1;
    info.samplerate = 44100;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_32;
    SNDFILE *file = sf_open(source.string().c_str(), SFM_WRITE, &info);
    REQUIRE(file != nullptr);
    std::vector<int> samples(65553);
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        samples[i] = 0x12345679 + int(i % 17);
    }
    REQUIRE(sf_writef_int(file, samples.data(), samples.size()) ==
            samples.size());
    REQUIRE(sf_close(file) == 0);
    auto cache = std::make_shared<storage::DecodedBlockCache>(
        2 * storage::AudioBlockBytes);
    int previews = 0;
    auto imported = file::importOwnedAudio(
        source, root / "working", cache, {}, {},
        [&](waveform::DecodedWaveformChunk chunk)
        {
            ++previews;
            REQUIRE(chunk.toBlock >= chunk.fromBlock);
        },
        options);
    REQUIRE(previews == 2);
    REQUIRE(imported.metadata.externalSamples);
    REQUIRE_FALSE(
        std::filesystem::equivalent(source, imported.audio->sourcePath()));
    DocumentSession session;
    REQUIRE_THROWS_AS(file::commitLoadedAudioFile(session, source.string(),
                                                  imported.metadata),
                      std::logic_error);
    std::ifstream original(source, std::ios::binary),
        owned(imported.audio->sourcePath(), std::ios::binary);
    REQUIRE(std::vector<char>(std::istreambuf_iterator<char>(original), {}) ==
            std::vector<char>(std::istreambuf_iterator<char>(owned), {}));
    original.close();
    owned.close();
    auto reference = file::loadAudioFile(source.string());
    storage::DocumentAudioReader reader(reference.document);
    std::vector<float> expected(samples.size()), actual(samples.size());
    reader.readChannel(0, 0, expected);
    imported.audio->readChannel(0, 0, actual);
    REQUIRE(actual == expected);
    gui::WaveformCache expectedPeaks;
    expectedPeaks.rebuildAll(expected.data(), expected.size());
    const auto &peaks = imported.metadata.waveformCaches.getCache(0);
    for (int level = 0; level < expectedPeaks.levelsCount(); ++level)
    {
        for (std::size_t i = 0; i < expectedPeaks.getLevelByIndex(level).size();
             ++i)
        {
            REQUIRE(peaks.getLevelByIndex(level)[i].min ==
                    expectedPeaks.getLevelByIndex(level)[i].min);
            REQUIRE(peaks.getLevelByIndex(level)[i].max ==
                    expectedPeaks.getLevelByIndex(level)[i].max);
        }
    }
    bool cancel = false;
    REQUIRE_THROWS_AS(file::importOwnedAudio(
                          source, root / "canceled", cache, {},
                          [&]
                          {
                              return cancel;
                          },
                          [&](waveform::DecodedWaveformChunk)
                          {
                              cancel = true;
                          },
                          options),
                      LongTaskCanceledError);
    REQUIRE_FALSE(std::filesystem::exists(root / "canceled"));
    REQUIRE(std::filesystem::exists(source));
    bool changed = false;
    REQUIRE_THROWS(file::importOwnedAudio(
        source, root / "changed", cache,
        [&](const std::string &, std::optional<double>)
        {
            if (!changed)
            {
                std::ofstream out(source, std::ios::binary | std::ios::app);
                out.put('x');
                changed = true;
            }
        },
        {}, {}, options));
    REQUIRE_FALSE(std::filesystem::exists(root / "changed"));
    std::filesystem::remove(source);
    imported.audio->readChannel(0, 65536, std::span<float>(actual).first(17));
    REQUIRE(actual[0] == expected[65536]);
}
