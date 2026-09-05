#include <catch2/catch_test_macros.hpp>
#include "storage/AudioEditRevision.hpp"
#include "TestPaths.hpp"
#include <random>

using namespace cupuacu;
using namespace cupuacu::storage;

namespace
{
    std::shared_ptr<const AudioRevision> makeSource(int64_t frames,
                                                    int bias = 0)
    {
        auto store = std::make_shared<AudioBlockStore>(
            test::makeUniqueTestRoot("edit-tree") / "store");
        auto cache = std::make_shared<DecodedBlockCache>(AudioBlockBytes);
        AudioRevisionBuilder builder({frames, 2, 48000, SampleFormat::PCM_S32},
                                     store, cache);
        std::vector<float> input(4096 * 2);
        for (int64_t start = 0; start < frames; start += 4096)
        {
            const auto count = std::min<int64_t>(4096, frames - start);
            for (int64_t i = 0; i < count; ++i)
            {
                for (int c = 0; c < 2; ++c)
                {
                    input[i * 2 + c] =
                        float((start + i + bias) % 1009 + c) / 1024;
                }
            }
            builder.appendInterleaved(
                std::span<const float>(input).first(count * 2));
        }
        return builder.finish();
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
