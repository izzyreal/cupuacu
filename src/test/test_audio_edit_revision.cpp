#include <catch2/catch_test_macros.hpp>
#include "storage/AudioEditRevision.hpp"
#include "TestPaths.hpp"
#include <random>
#include <future>
#include <barrier>

using namespace cupuacu;
using namespace cupuacu::storage;

namespace
{
    std::shared_ptr<const AudioRevision>
    makeSource(int64_t frames, int bias = 0, bool withPeaks = true)
    {
        auto store = std::make_shared<AudioBlockStore>(
            test::makeUniqueTestRoot("edit-tree") / "store");
        auto cache = std::make_shared<DecodedBlockCache>(AudioBlockBytes);
        AudioRevisionBuilder builder({frames, 2, 48000, SampleFormat::PCM_S32},
                                     store, cache);
        std::vector<float> input(4096 * 2);
        std::vector<std::vector<float>> planar(2, std::vector<float>(frames));
        for (int64_t start = 0; start < frames; start += 4096)
        {
            const auto count = std::min<int64_t>(4096, frames - start);
            for (int64_t i = 0; i < count; ++i)
            {
                for (int c = 0; c < 2; ++c)
                {
                    input[i * 2 + c] =
                        float((start + i + bias) % 1009 + c) / 1024;
                    planar[c][start + i] = input[i * 2 + c];
                }
            }
            builder.appendInterleaved(
                std::span<const float>(input).first(count * 2));
        }
        std::shared_ptr<const waveform::SourcePeaks> peaks;
        if (withPeaks)
        {
            std::vector<std::vector<gui::PeakLevel>> levels;
            for (int c = 0; c < 2; ++c)
            {
                gui::WaveformCache cache;
                cache.rebuildAll(planar[c].data(), frames);
                levels.push_back(cache.snapshotBuildState().levels);
            }
            peaks = std::make_shared<waveform::SourcePeaks>(
                AudioShape{frames, 2, 48000, SampleFormat::PCM_S32},
                std::move(levels));
        }
        return builder.finish({}, std::move(peaks));
    }
    using Samples = std::vector<std::vector<float>>;
    Samples read(const AudioReader &reader)
    {
        Samples result(reader.shape().channels,
                       std::vector<float>(reader.shape().frames));
        for (int c = 0; c < reader.shape().channels; ++c)
        {
            reader.readChannel(c, 0, result[c]);
        }
        return result;
    }
    auto slice(const AudioEditRevision &revision, int64_t start, int64_t count)
    {
        AudioEditTransaction edit(revision);
        edit.trim(start, count);
        return edit.finish();
    }
} // namespace

TEST_CASE(
    "Reference edits preserve block boundaries, source provenance and "
    "ownership",
    "[audio-edit-tree]")
{
    auto source = makeSource(2 * AudioBlockFrames + 31);
    auto base = AudioEditRevision::from(source);
    const auto original = read(*source);
    const auto ioBefore = source->blockStore()->ioBytes();
    auto clipboard = slice(*base, AudioBlockFrames - 3, 19);
    AudioEditTransaction cut(*base);
    cut.erase(AudioBlockFrames - 3, 19);
    auto removed = cut.finish();
    AudioEditTransaction paste(*removed);
    paste.insert(AudioBlockFrames - 3, *clipboard);
    auto restored = paste.finish();
    REQUIRE(source->blockStore()->ioBytes() == ioBefore);
    REQUIRE(read(*restored) == original);
    int64_t visited = 0;
    restored->visitSourceRanges(1, AudioBlockFrames - 10, 41,
                                [&](const auto &range)
                                {
                                    REQUIRE(range.source == source);
                                    REQUIRE(range.channel == 1);
                                    REQUIRE(range.start ==
                                            AudioBlockFrames - 10 + visited);
                                    visited += range.frames;
                                });
    REQUIRE(visited == 41);
    AudioEditTransaction silence(*base);
    silence.replaceChannel(1, AudioBlockFrames - 1, 3);
    auto silenced = silence.finish();
    auto expected = original;
    std::fill_n(expected[1].begin() + AudioBlockFrames - 1, 3, 0.0f);
    REQUIRE(read(*silenced) == expected);
    REQUIRE(read(*base) == original);
    const auto path = source->blockStore()->path();
    source.reset();
    base.reset();
    removed.reset();
    restored.reset();
    silenced.reset();
    REQUIRE(std::filesystem::exists(
        path)); // Clipboard/transaction snapshots pin storage.
    REQUIRE(read(*clipboard)[1][0] == original[1][AudioBlockFrames - 3]);
}

TEST_CASE("Randomized tree edits and undo redo match contiguous samples",
          "[audio-edit-tree]")
{
    auto source = makeSource(4096);
    auto otherSource = makeSource(257, 91);
    auto base = AudioEditRevision::from(source);
    auto other = AudioEditRevision::from(otherSource);
    auto current = slice(*base, 0, 1024);
    Samples expected = read(*current), otherSamples = read(*other);
    std::vector<std::pair<std::shared_ptr<const AudioEditRevision>, Samples>>
        history;
    std::mt19937 random(39419);
    for (int step = 0; step < 400; ++step)
    {
        history.emplace_back(current, expected);
        const auto frames = expected[0].size();
        const auto at = random() % (frames + 1);
        const auto count = std::min<std::size_t>(frames - at, random() % 127);
        AudioEditTransaction edit(*current);
        const auto beforeIO = source->blockStore()->ioBytes();
        const auto otherIO = otherSource->blockStore()->ioBytes();
        switch (random() % 5)
        {
            case 0:
                edit.erase(at, count);
                for (auto &channel : expected)
                {
                    channel.erase(channel.begin() + at,
                                  channel.begin() + at + count);
                }
                break;
            case 1:
                edit.insert(at, *other);
                for (int c = 0; c < 2; ++c)
                {
                    expected[c].insert(expected[c].begin() + at,
                                       otherSamples[c].begin(),
                                       otherSamples[c].end());
                }
                break;
            case 2:
                edit.replaceChannel(0, at, count);
                std::fill_n(expected[0].begin() + at, count, 0.0f);
                break;
            case 3:
                edit.replaceChannel(1, at, count, other.get(), 0, 3);
                std::copy_n(otherSamples[0].begin() + 3, count,
                            expected[1].begin() + at);
                break;
            case 4:
                edit.trim(at, frames - at);
                for (auto &channel : expected)
                {
                    channel.erase(channel.begin(), channel.begin() + at);
                }
                break;
        }
        current = edit.finish();
        REQUIRE(source->blockStore()->ioBytes() == beforeIO);
        REQUIRE(otherSource->blockStore()->ioBytes() == otherIO);
        REQUIRE(read(*current) == expected);
        REQUIRE(current->indexHeight() < 32);
        AudioEditRevision::PeakWork work;
        REQUIRE(current->prepareWaveform(work));
        for (int c = 0; c < 2 && current->shape().frames; ++c)
        {
            auto peak = current->queryWaveformOverview(
                c, 0, current->shape().frames, work);
            REQUIRE(peak);
            const auto [minimum, maximum] =
                std::minmax_element(expected[c].begin(), expected[c].end());
            REQUIRE(peak->min == *minimum);
            REQUIRE(peak->max == *maximum);
        }
    }
    // Undo/redo is a reference change. Check every retained revision twice.
    for (auto it = history.rbegin(); it != history.rend(); ++it)
    {
        REQUIRE(read(*it->first) == it->second);
    }
    for (const auto &[revision, samples] : history)
    {
        REQUIRE(read(*revision) == samples);
    }
}

TEST_CASE(
    "Adversarial insertion stays balanced and large logical audio uses bounded "
    "edits",
    "[audio-edit-tree]")
{
    auto source = makeSource(17);
    auto piece = AudioEditRevision::from(source);
    auto current = piece;
    uint64_t maxNodes = 0;
    for (int i = 0; i < 4096; ++i)
    {
        AudioEditTransaction edit(*current);
        edit.insert(i % 2 ? current->shape().frames / 2 : 0, *piece);
        maxNodes = std::max(maxNodes, edit.allocatedIndexNodes());
        current = edit.finish();
    }
    REQUIRE(current->indexHeight() < 24);
    REQUIRE(maxNodes < 256);
    // Exponentially reuse existing subtrees, without an index/sample expansion.
    for (int i = 0; i < 24; ++i)
    {
        AudioEditTransaction edit(*current);
        edit.insert(current->shape().frames, *current);
        current = edit.finish();
    }
    REQUIRE(current->shape().frames > int64_t(1) << 40);
    const auto ioBefore = source->blockStore()->ioBytes();
    AudioEditTransaction edit(*current);
    edit.erase(1, 3);
    auto shortened = edit.finish();
    REQUIRE(edit.allocatedIndexNodes() < 1024);
    REQUIRE(shortened->shape().frames == current->shape().frames - 3);
    REQUIRE(source->blockStore()->ioBytes() == ioBefore);
    std::array<float, 7> before{}, after{};
    current->readChannel(0, 4, before);
    shortened->readChannel(0, 1, after);
    REQUIRE(before == after);
    AudioEditRevision::PeakWork peaks;
    REQUIRE(shortened->prepareWaveform(peaks));
    REQUIRE(peaks.preparedNodes <
            40000); // Unique shared nodes, not logical length.
    AudioEditRevision::PeakWork overview;
    auto peak = shortened->queryWaveformOverview(
        0, 0, shortened->shape().frames, overview);
    REQUIRE(peak);
    REQUIRE(peak->min == 0.0f);
    REQUIRE(peak->max == 16.0f / 1024);
    REQUIRE(overview.visitedNodes == 1);
}

TEST_CASE("Invalid and overflowing edits leave the candidate revision intact",
          "[audio-edit-tree]")
{
    auto source = makeSource(1);
    auto current = AudioEditRevision::from(source);
    for (int i = 0; i < 62; ++i)
    {
        AudioEditTransaction edit(*current);
        edit.insert(current->shape().frames, *current);
        current = edit.finish();
    }
    AudioEditTransaction edit(*current);
    REQUIRE_THROWS_AS(edit.insert(0, *current), std::overflow_error);
    REQUIRE_THROWS_AS(edit.erase(-1, 1), std::out_of_range);
    REQUIRE_THROWS_AS(edit.trim(0, -1), std::out_of_range);
    REQUIRE_THROWS_AS(edit.replaceChannel(2, 0, 1), std::out_of_range);
    REQUIRE_THROWS_AS(edit.replaceChannel(0, 0, 1, current.get(), 2, 0),
                      std::out_of_range);
    REQUIRE(edit.finish()->shape().frames == current->shape().frames);
    std::array<float, 1> sample{};
    edit.finish()->readChannel(0, current->shape().frames - 1, sample);
    REQUIRE(sample[0] == 0.0f);
}

TEST_CASE("A copied range keeps owned storage alive until its final reference",
          "[audio-edit-tree]")
{
    std::shared_ptr<const AudioEditRevision> clipboard;
    std::filesystem::path path;
    {
        auto source = makeSource(17);
        path = source->blockStore()->path();
        auto base = AudioEditRevision::from(source);
        clipboard = slice(*base, 3, 5);
    }
    REQUIRE(std::filesystem::exists(path));
    clipboard.reset();
    REQUIRE_FALSE(std::filesystem::exists(path));
}

TEST_CASE(
    "Waveform summaries exclude deleted spikes and never read samples while "
    "querying",
    "[audio-edit-tree]")
{
    std::vector<float> samples(65536 + 39, 0.25f);
    samples[129] = 100.0f;
    samples[139] = -200.0f;
    samples.back() = 0.5f;
    const AudioShape shape{int64_t(samples.size()), 1, 48000,
                           SampleFormat::FLOAT32};
    auto store = std::make_shared<AudioBlockStore>(
        test::makeUniqueTestRoot("spike-peaks") / "store");
    AudioRevisionBuilder builder(
        shape, store, std::make_shared<DecodedBlockCache>(AudioBlockBytes));
    builder.appendInterleaved(samples);
    gui::WaveformCache cache;
    cache.rebuildAll(samples.data(), samples.size());
    auto source =
        builder.finish({}, std::make_shared<waveform::SourcePeaks>(
                               shape, std::vector<std::vector<gui::PeakLevel>>{
                                          cache.snapshotBuildState().levels}));
    auto base = AudioEditRevision::from(source);
    AudioEditRevision::PeakWork initial;
    REQUIRE_FALSE(base->queryWaveformOverview(0, 0, shape.frames, initial));
    REQUIRE(base->prepareWaveform(initial));
    REQUIRE(initial.boundarySamples ==
            0); // Also reuses the final partial bucket.
    auto before = store->ioBytes();
    AudioEditTransaction edit(*base);
    edit.erase(129, 1);
    edit.erase(138, 1);
    auto changed = edit.finish();
    REQUIRE(store->ioBytes() == before);
    REQUIRE_FALSE(
        changed->queryWaveformOverview(0, 0, changed->shape().frames, initial));
    AudioEditRevision::PeakWork preparation;
    REQUIRE(changed->prepareWaveform(preparation));
    REQUIRE(preparation.boundarySamples < 1024);
    auto expected = samples;
    expected.erase(expected.begin() + 129);
    expected.erase(expected.begin() + 138);
    before = store->ioBytes();
    AudioEditRevision::PeakWork work;
    auto whole =
        changed->queryWaveformOverview(0, 0, changed->shape().frames, work);
    REQUIRE(whole);
    REQUIRE(whole->min == 0.25f);
    REQUIRE(whole->max == 0.5f);
    REQUIRE(work.visitedNodes == 1);
    for (int64_t start = 0; start < 512; ++start)
    {
        auto peak = changed->queryWaveformOverview(0, start, 129, work);
        REQUIRE(peak);
        REQUIRE(peak->min == 0.25f);
        REQUIRE(peak->max ==
                0.25f); // Neither deleted spike leaks through rounding.
    }
    REQUIRE(store->ioBytes() == before);
    AudioEditRevision::PeakWork reused;
    REQUIRE(changed->prepareWaveform(reused));
    REQUIRE(reused.visitedNodes == 1);
    REQUIRE(reused.preparedNodes == 0);
    REQUIRE(reused.boundarySamples == 0);
    // Two copies share the prepared tree, including its exact aggregate.
    AudioEditTransaction twice(*changed);
    twice.insert(changed->shape().frames, *changed);
    auto doubled = twice.finish();
    AudioEditRevision::PeakWork doubledWork;
    REQUIRE(doubled->queryWaveformOverview(0, 0, changed->shape().frames,
                                           doubledWork));
    REQUIRE(doubled->prepareWaveform(doubledWork));
    REQUIRE(doubledWork.preparedNodes == 1);
    REQUIRE(doubledWork.boundarySamples == 0);
}

TEST_CASE(
    "Overview pyramids extend beyond sixteen levels with shared source pages",
    "[audio-edit-tree]")
{
    constexpr int64_t blocks = (1 << 17) + 7;
    std::vector<gui::PeakLevel> levels(1);
    levels[0].resize(blocks);
    for (int64_t i = 0; i < blocks; ++i)
    {
        levels[0].set(i, {-0.25f, 0.25f});
    }
    levels[0].set(0, {-1.0f, 0.25f});
    levels[0].set(blocks - 1, {-0.25f, 1.0f});
    for (int i = 1; i < 16; ++i)
    {
        gui::PeakLevel next;
        next.resize((levels.back().size() + 1) / 2);
        for (std::size_t p = 0; p < next.size(); ++p)
        {
            next.set(p, p * 2 + 1 < levels.back().size()
                            ? waveform::combine(levels.back()[p * 2],
                                                levels.back()[p * 2 + 1])
                            : levels.back()[p * 2]);
        }
        levels.push_back(std::move(next));
    }
    waveform::SourcePeaks peaks({blocks * 128, 1, 48000, SampleFormat::FLOAT32},
                                {levels});
    levels[0].set(
        0, {-99, 99}); // Copy-on-write keeps the attached source immutable.
    uint64_t visited = 0;
    auto result = peaks.queryBlocks(0, 0, blocks, visited);
    REQUIRE(result.min == -1.0f);
    REQUIRE(result.max == 1.0f);
    REQUIRE(visited < 20);
    REQUIRE_THROWS_AS(peaks.queryBlocks(0, 0, blocks + 1, visited),
                      std::out_of_range);
}

TEST_CASE(
    "Missing and canceled waveform work stays pending without raw fallbacks",
    "[audio-edit-tree]")
{
    auto missingSource = makeSource(257, 0, false);
    auto missing = AudioEditRevision::from(missingSource);
    const auto before = missingSource->blockStore()->ioBytes();
    AudioEditRevision::PeakWork work;
    REQUIRE_FALSE(missing->prepareWaveform(work));
    REQUIRE_FALSE(missing->queryWaveformOverview(0, 0, 257, work));
    REQUIRE(missingSource->blockStore()->ioBytes() == before);
    auto source = makeSource(257);
    auto base = AudioEditRevision::from(source);
    AudioEditTransaction edit(*base);
    edit.erase(1, 3);
    auto changed = edit.finish();
    const auto beforeCancel = source->blockStore()->ioBytes();
    REQUIRE_FALSE(changed->prepareWaveform(work,
                                           []
                                           {
                                               return true;
                                           }));
    REQUIRE_FALSE(changed->queryWaveformOverview(0, 0, 254, work));
    REQUIRE(source->blockStore()->ioBytes() == beforeCancel);
    REQUIRE(changed->prepareWaveform(work));
    REQUIRE(changed->queryWaveformOverview(0, 0, 254, work));
}

TEST_CASE("Concurrent summary preparation safely publishes immutable results",
          "[audio-edit-tree]")
{
    auto source = makeSource(65536 + 39);
    auto base = AudioEditRevision::from(source);
    AudioEditTransaction edit(*base);
    edit.erase(11, 17);
    auto changed = edit.finish();
    std::barrier start(3);
    const auto prepare = [&]
    {
        start.arrive_and_wait();
        AudioEditRevision::PeakWork work;
        return changed->prepareWaveform(work);
    };
    auto first = std::async(std::launch::async, prepare);
    auto second = std::async(std::launch::async, prepare);
    start.arrive_and_wait();
    for (int i = 0; i < 1000; ++i)
    {
        AudioEditRevision::PeakWork work;
        auto peak =
            changed->queryWaveformOverview(0, 0, changed->shape().frames, work);
        if (peak)
        {
            REQUIRE(peak->min == 0.0f);
            REQUIRE(peak->max == 1008.0f / 1024);
        }
    }
    REQUIRE(first.get());
    REQUIRE(second.get());
}
