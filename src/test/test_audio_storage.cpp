#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "Document.hpp"
#include "storage/PagedArray.hpp"

#include <algorithm>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

TEST_CASE(
    "Paged samples preserve snapshots through overlapping moves and resizing",
    "[audio][document]")
{
    using Array = cupuacu::storage::PagedArray<
        float, 16, cupuacu::performance::Work::SampleBytesCopied>;
    std::vector<Array> versions(3);
    std::vector<std::vector<float>> expected(3);
    std::mt19937 random(1809);
    for (int step = 0; step < 600; ++step)
    {
        const auto a = random() % 3, b = random() % 3;
        switch (random() % 5)
        {
            case 0:
                versions[a] = versions[b];
                expected[a] = expected[b];
                break;
            case 1:
            {
                const auto size = random() % 99;
                versions[a].resize(size);
                expected[a].resize(size);
                break;
            }
            case 2:
                if (!expected[a].empty())
                {
                    const auto index = random() % expected[a].size();
                    versions[a].set(index, float(step));
                    expected[a][index] = float(step);
                }
                break;
            case 3:
                if (!expected[a].empty())
                {
                    const auto source = random() % expected[a].size();
                    const auto destination = random() % expected[a].size();
                    const auto size =
                        std::min(expected[a].size() - source,
                                 expected[a].size() - destination);
                    versions[a].copyWithin(destination, source, size);
                    std::memmove(expected[a].data() + destination,
                                 expected[a].data() + source,
                                 size * sizeof(float));
                }
                break;
            case 4:
                versions[a].assign(33, float(step));
                expected[a].assign(33, float(step));
                break;
        }
        for (std::size_t v = 0; v < versions.size(); ++v)
        {
            REQUIRE(versions[v].size() == expected[v].size());
            const auto view = versions[v].view();
            for (std::size_t i = 0; i < expected[v].size(); ++i)
            {
                REQUIRE(view[i] == expected[v][i]);
            }
        }
    }
}

TEST_CASE("Document page edits isolate audio dirty bits and provenance",
          "[audio][document]")
{
    const int channelCount = GENERATE(1, 2, 3, 9);
    cupuacu::Document original;
    original.initialize(cupuacu::SampleFormat::PCM_S16, 44100, channelCount,
                        32781);
    std::vector<float> samples(32781, 0.25f);
    for (int channel = 0; channel < channelCount; ++channel)
    {
        original.writeChannelFloatBlock(channel, 0, samples.data(),
                                        samples.size(), false);
    }
    original.markCurrentStateAsSavedSource();
    auto edited = original;
    auto retainedPlayback = original.getAudioBuffer();
    std::vector<float> replacement(25, -0.5f);
    edited.writeChannelFloatBlock(channelCount - 1, 16380, replacement.data(),
                                  replacement.size(), true);
    const auto sourceId = original.getPreservationSourceId();
    const auto oldLease = original.acquireReadLease();
    const auto newLease = edited.acquireReadLease();
    for (int channel = 0; channel < channelCount; ++channel)
    {
        for (int64_t frame :
             {0, 16379, 16380, 16383, 16384, 16404, 16405, 32780})
        {
            const bool changed =
                channel == channelCount - 1 && frame >= 16380 && frame < 16405;
            REQUIRE(newLease.getSample(channel, frame) ==
                    (changed ? -0.5f : 0.25f));
            REQUIRE(newLease.isDirty(channel, frame) == changed);
            REQUIRE_FALSE(oldLease.isDirty(channel, frame));
            REQUIRE(oldLease.getSample(channel, frame) == 0.25f);
            REQUIRE(retainedPlayback->getSample(channel, frame) == 0.25f);
            const auto provenance =
                newLease.getSampleProvenance(channel, frame);
            REQUIRE(provenance.sourceId == sourceId);
            REQUIRE(provenance.frameIndex == frame);
        }
    }
}

TEST_CASE(
    "Paged interleaved writes and structural changes preserve old revisions",
    "[audio][document]")
{
    cupuacu::Document document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 2, 32781);
    std::vector<float> source(32781 * 2);
    for (std::size_t i = 0; i < source.size(); ++i)
    {
        source[i] = float(i % 127) / 128;
    }
    document.writeInterleavedFloatBlock(0, source.data(), 32781, 2, false);
    auto original = document;
    document.insertFrames(16380, 19);
    document.removeFrames(16370, 43);
    document.insertFrames(document.getFrameCount(), 11);
    auto expected = source;
    expected.insert(expected.begin() + 16380 * 2, 19 * 2, 0.0f);
    expected.erase(expected.begin() + 16370 * 2, expected.begin() + 16413 * 2);
    expected.insert(expected.end(), 11 * 2, 0.0f);
    REQUIRE(document.getFrameCount() * 2 == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        REQUIRE(document.getSample(i % 2, i / 2) == expected[i]);
    }
    for (std::size_t i = 0; i < source.size(); ++i)
    {
        REQUIRE(original.getSample(i % 2, i / 2) == source[i]);
    }
}

TEST_CASE("Concurrent document revisions keep independent sample pages",
          "[audio][document][threading]")
{
    cupuacu::Document original;
    original.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 2, 32781);
    const std::vector<float> initial(32781, 0.25f);
    original.writeChannelFloatBlock(0, 0, initial.data(), initial.size(),
                                    false);
    auto first = original;
    auto second = original;
    const std::vector<float> positive(32781, 0.5f), negative(32781, -0.5f);
    std::thread a(
        [&]
        {
            first.writeChannelFloatBlock(0, 0, positive.data(),
                                         positive.size());
        });
    std::thread b(
        [&]
        {
            second.writeChannelFloatBlock(0, 0, negative.data(),
                                          negative.size());
        });
    a.join();
    b.join();
    for (int64_t frame : {0, 16383, 16384, 32780})
    {
        REQUIRE(original.getSample(0, frame) == 0.25f);
        REQUIRE(first.getSample(0, frame) == 0.5f);
        REQUIRE(second.getSample(0, frame) == -0.5f);
        REQUIRE(first.getSample(1, frame) == 0);
    }
}
