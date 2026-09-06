#include <catch2/catch_test_macros.hpp>
#include "storage/AudioEditRevision.hpp"
#include "storage/RevisionArchive.hpp"
#include "storage/AsyncAudioReader.hpp"
#include "waveform/WaveformViewport.hpp"
#include "TestPaths.hpp"
#include <future>

using namespace cupuacu;
using namespace cupuacu::storage;

namespace
{
    using Json = nlohmann::json;
    Json readRecord(const std::filesystem::path &path, uint64_t id)
    {
        std::ifstream in(path, std::ios::binary);
        in.seekg(id);
        uint32_t count = 0;
        for (int i = 0; i < 4; ++i)
        {
            count |= uint32_t(in.get()) << (i * 8);
        }
        in.seekg(4, std::ios::cur);
        std::vector<uint8_t> bytes(count);
        in.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
        if (!in)
        {
            throw std::runtime_error("Test record read failed");
        }
        return Json::from_cbor(bytes);
    }
    uint64_t appendRecord(const std::filesystem::path &path, const Json &record)
    {
        const auto id = std::filesystem::file_size(path);
        const auto bytes = Json::to_cbor(record);
        uint32_t hash = 2166136261u;
        for (auto b : bytes)
        {
            hash = (hash ^ b) * 16777619u;
        }
        std::ofstream out(path, std::ios::binary | std::ios::app);
        for (auto word : {uint32_t(bytes.size()), hash})
        {
            for (int i = 0; i < 4; ++i)
            {
                out.put(char(word >> (i * 8)));
            }
        }
        out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        return id;
    }
} // namespace

TEST_CASE("Working indexes retain two pages across large shared directories",
          "[record-index]")
{
    RecordIndex<AudioBlock> index;
    constexpr uint64_t count = 1000000;
    for (uint64_t i = 0; i < count; ++i)
    {
        index.push_back(
            {i / 256, (i % 256) * AudioBlockBytes, uint32_t(AudioBlockFrames)});
    }
    index.setBack({123, 456, 7});
    index.seal();
    auto shared = index;
    for (uint64_t i = 0; i < count - 1; i += 997)
    {
        const auto value = shared[i];
        REQUIRE(value.segment == i / 256);
        REQUIRE(value.offset == (i % 256) * AudioBlockBytes);
    }
    CHECK(shared.back().frames == 7);
    CHECK(index.stats().residentBytes <= 512 * sizeof(AudioBlock));
    CHECK(shared.stats().diskBytes == index.stats().diskBytes);
    REQUIRE_THROWS(index.push_back({}));
    REQUIRE_THROWS(index.setBack({}));
    REQUIRE_THROWS(index[count]);
}

TEST_CASE("Published working index prefixes survive concurrent appends",
          "[record-index]")
{
    RecordIndex<uint64_t> index;
    auto published = index;
    std::atomic<uint64_t> count{0};
    auto writer =
        std::async(std::launch::async,
                   [&]
                   {
                       for (uint64_t i = 0; i < 20000; ++i)
                       {
                           index.push_back(i * 13);
                           count.store(i + 1, std::memory_order_release);
                       }
                       index.seal();
                   });
    for (uint64_t i = 0; i < 20000; ++i)
    {
        while (count.load(std::memory_order_acquire) <= i)
        {
            std::this_thread::yield();
        }
        REQUIRE(published[i] == i * 13);
    }
    writer.get();
}

TEST_CASE(
    "Paged provenance shares cache rebinding and survives archive recovery",
    "[record-index]")
{
    const auto path = test::makeUniqueTestRoot("paged-provenance");
    const AudioShape shape{65539, 1, 48000, SampleFormat::PCM_S32};
    auto store = std::make_shared<AudioBlockStore>(path / "working");
    auto cache = std::make_shared<DecodedBlockCache>(AudioBlockBytes);
    AudioRevisionBuilder builder(shape, store, cache);
    std::vector<float> samples(shape.frames, .25f);
    builder.appendInterleaved(samples);
    std::array<audio::SampleProvenance, 1024> provenance;
    std::array<uint8_t, 1024> dirty;
    for (int64_t first = 0; first < shape.frames; first += 1024)
    {
        const auto take = std::min<int64_t>(1024, shape.frames - first);
        for (int64_t i = 0; i < take; ++i)
        {
            provenance[i] = {(first + i) % 3 == 0 ? 12u : 21u, first + i};
            dirty[i] = (first + i) % 2;
        }
        builder.appendChannelMetadata(0, first,
                                      std::span(provenance).first(take),
                                      std::span(dirty).first(take));
    }
    auto original = builder.finish({}, {}, 12);
    auto rebound = original->withSampleCache(cache, {}, 42)
                       ->withSampleCache(cache, {}, 43);
    CHECK(original->indexStats().residentBytes <=
          256 * sizeof(AudioBlock) + 512 * 40);
    CHECK(rebound->indexStats().diskBytes == original->indexStats().diskBytes);
    auto archive = RevisionArchive::open(path / "manifest");
    const auto id = archive->saveSource(rebound);
    archive->commit({{"source", id}});
    CHECK(archive->readManifest().at("version") == 2);
    auto restored = archive->loadSource(id);
    for (int64_t first : {0, 255, 1023, 65001})
    {
        const auto take = std::min<int64_t>(1024, shape.frames - first);
        restored->readLegacyMetadata(0, first,
                                     std::span(provenance).first(take),
                                     std::span(dirty).first(take));
        for (int64_t i = 0; i < take; ++i)
        {
            CHECK(provenance[i].sourceId == ((first + i) % 3 == 0 ? 43 : 21));
            CHECK(provenance[i].frameIndex == first + i);
            CHECK(dirty[i] == (first + i) % 2);
        }
    }
    original->readLegacyMetadata(0, 0, std::span(provenance).first(1),
                                 std::span(dirty).first(1));
    CHECK(provenance[0].sourceId == 12);
    auto edited = AudioEditRevision::from(restored);
    waveform::ViewportSource viewport;
    viewport.audio = edited;
    auto view = waveform::WaveformViewport::compute(viewport, {0, 255, .5, 32},
                                                    []
                                                    {
                                                        return false;
                                                    });
    REQUIRE(view);
    REQUIRE(view->dirty.size() == view->samples.size());
    for (std::size_t i = 0; i < view->dirty.size(); ++i)
    {
        CHECK(view->dirty[i] == (view->rawStart + i) % 2);
    }
    RevisionArchive::remove(path / "manifest");
    float sample = 0;
    restored->readChannel(0, shape.frames - 1, {&sample, 1});
    CHECK(sample == .25f);
}

TEST_CASE("Progressive and completed audio share paged block indexes",
          "[record-index]")
{
    const auto path = test::makeUniqueTestRoot("paged-blocks");
    const AudioShape shape{257 * AudioBlockFrames + 3, 1, 48000,
                           SampleFormat::FLOAT32};
    auto store =
        std::make_shared<AudioBlockStore>(path / "working", AudioBlockBytes);
    auto cache = std::make_shared<DecodedBlockCache>(AudioBlockBytes);
    auto preview = std::make_shared<ImportAudioReader>(shape, store, cache);
    AudioRevisionBuilder builder(shape, store, cache, {}, preview);
    std::vector<float> samples(AudioBlockFrames, .375f);
    for (int64_t first = 0; first < shape.frames; first += AudioBlockFrames)
    {
        builder.appendInterleaved(std::span(samples).first(
            std::min<int64_t>(AudioBlockFrames, shape.frames - first)));
    }
    auto source = builder.finish();
    CHECK(preview->blockIndexes()[0].stats().records == 258);
    CHECK(preview->blockIndexes()[0].stats().diskBytes ==
          source->indexStats().diskBytes);
    auto archive = RevisionArchive::open(path / "manifest");
    auto id = archive->saveSource(source);
    archive->commit({{"source", id}});
    archive->readManifest();
    auto restored = archive->loadSource(id);
    for (int64_t at : {int64_t(0), 255 * AudioBlockFrames, shape.frames - 1})
    {
        float value = 0;
        restored->readChannel(0, at, {&value, 1});
        CHECK(value == .375f);
        preview->readChannel(0, at, {&value, 1});
        CHECK(value == .375f);
    }
    RevisionArchive::remove(path / "manifest");
}

TEST_CASE("Repeated reference restoration coalesces edit index boundaries",
          "[record-index]")
{
    auto base = AudioEditRevision::silence(
        {1000000000, 1, 48000, SampleFormat::FLOAT32});
    for (int i = 0; i < 2000; ++i)
    {
        AudioEditTransaction copy(*base);
        copy.trim(i * 101, 17);
        const auto clip = copy.finish();
        AudioEditTransaction edit(*base);
        edit.erase(i * 101, 17);
        edit.insert(i * 101, *clip);
        base = edit.finish();
        REQUIRE(base->indexHeight() == 1);
    }
}

TEST_CASE("Version one source metadata and peak references remain readable",
          "[record-index]")
{
    const auto path = test::makeUniqueTestRoot("old-source-index");
    auto cache = std::make_shared<DecodedBlockCache>(AudioBlockBytes);
    AudioShape shape{129, 1, 48000, SampleFormat::FLOAT32};
    auto store = std::make_shared<AudioBlockStore>(path / "working");
    AudioRevisionBuilder builder(shape, store, cache);
    builder.appendInterleaved(std::vector<float>(129, .75f));
    auto peaks = waveform::SourcePeaks::createStreaming(
        shape,
        [](int, uint64_t, std::span<waveform::Peak> values)
        {
            std::fill(values.begin(), values.end(), waveform::Peak{.75f, .75f});
        },
        cache);
    auto source = builder.finish({}, peaks);
    auto archive = RevisionArchive::open(path / "manifest");
    const auto current = archive->saveSource(source);
    archive->commit({{"source", current}});
    auto manifest = archive->readManifest();
    const auto index = path / "manifest.revisions" /
                       manifest.at("generation").get<std::string>() /
                       "index.bin";
    auto legacy = readRecord(index, current);
    legacy.erase("metadataSourceId");
    for (auto &channel : legacy["peaks"])
    {
        for (auto &level : channel)
        {
            level["pages"] = Json::array({level.at("first")});
            level.erase("first");
            level.erase("pageCount");
        }
    }
    const auto id = appendRecord(index, legacy);
    manifest["source"] = id;
    manifest["logEnd"] = std::filesystem::file_size(index);
    manifest["version"] = 1;
    std::ofstream(path / "manifest") << manifest.dump();
    archive->readManifest();
    auto restored = archive->loadSource(id);
    float value = 0;
    restored->readChannel(0, 128, {&value, 1});
    CHECK(value == .75f);
    uint64_t visited = 0;
    CHECK(restored->sourcePeaks()->queryBlocks(0, 0, 2, visited).min == .75f);
    RevisionArchive::remove(path / "manifest");
}

TEST_CASE("Damaged index pages cannot expose an incomplete revision",
          "[record-index]")
{
    const auto path = test::makeUniqueTestRoot("damaged-source-index");
    auto cache = std::make_shared<DecodedBlockCache>(0);
    auto store = std::make_shared<AudioBlockStore>(path / "working");
    AudioRevisionBuilder builder({513, 1, 48000, SampleFormat::FLOAT32}, store,
                                 cache);
    builder.appendInterleaved(std::vector<float>(513, .25f));
    std::vector<audio::SampleProvenance> provenance(513);
    std::vector<uint8_t> dirty(513);
    for (int i = 0; i < 513; ++i)
    {
        dirty[i] = i % 2;
    }
    builder.appendChannelMetadata(0, 0, provenance, dirty);
    auto archive = RevisionArchive::open(path / "manifest");
    const auto id = archive->saveSource(builder.finish());
    archive->commit({{"source", id}});
    auto manifest = archive->readManifest();
    const auto index = path / "manifest.revisions" /
                       manifest.at("generation").get<std::string>() /
                       "index.bin";
    const auto record = readRecord(index, id);
    const auto page = record.at("metadata").at(0).at("first").get<uint64_t>();
    std::fstream damaged(index,
                         std::ios::binary | std::ios::in | std::ios::out);
    damaged.seekp(page + 8);
    damaged.put('\0');
    damaged.close();
    REQUIRE_THROWS(archive->loadSource(id));
    CHECK(archive->readManifest().at("source") == id);
    RevisionArchive::remove(path / "manifest");
}

TEST_CASE("Restoring older roots cannot shorten a shared archive store",
          "[record-index]")
{
    const auto path = test::makeUniqueTestRoot("index-store-prefix");
    auto cache = std::make_shared<DecodedBlockCache>(0);
    auto store = std::make_shared<AudioBlockStore>(path / "working");
    AudioRevisionBuilder first({129, 1, 48000, SampleFormat::FLOAT32}, store,
                               cache);
    first.appendInterleaved(std::vector<float>(129, .25f));
    auto older = first.finish();
    auto archive = RevisionArchive::open(path / "manifest");
    const auto oldId = archive->saveSource(older);
    AudioRevisionBuilder second({513, 1, 48000, SampleFormat::FLOAT32}, store,
                                cache);
    second.appendInterleaved(std::vector<float>(513, .75f));
    const auto newId = archive->saveSource(second.finish());
    archive->commit({{"source", newId}});
    archive->readManifest();
    const auto newer = archive->loadSource(newId);
    const auto loadedOld = archive->loadSource(oldId);
    const auto written = archive->stats.sampleBytes;
    archive->saveSource(loadedOld->withSampleCache(cache));
    CHECK(archive->stats.sampleBytes == written);
    float sample = 0;
    newer->readChannel(0, 512, {&sample, 1});
    CHECK(sample == .75f);
    RevisionArchive::remove(path / "manifest");
}
