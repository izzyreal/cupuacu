#include <catch2/catch_test_macros.hpp>

#include "Document.hpp"
#include "audio/AudioBuffer.hpp"

#include <chrono>
#include <future>
#include <utility>
#include <vector>

TEST_CASE("Document markers are assigned stable ids and clamped to bounds",
          "[document][markers]")
{
    cupuacu::Document document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 100);

    const uint64_t introId = document.addMarker(12, "Intro");
    const uint64_t outroId = document.addMarker(999, "Outro");

    REQUIRE(introId == 1);
    REQUIRE(outroId == 2);
    REQUIRE(document.getMarkers().size() == 2);
    REQUIRE(document.getMarkers()[0] ==
            cupuacu::DocumentMarker{
                .id = introId,
                .frame = 12,
                .label = "Intro",
            });
    REQUIRE(document.getMarkers()[1] ==
            cupuacu::DocumentMarker{
                .id = outroId,
                .frame = 100,
                .label = "Outro",
            });
}

TEST_CASE("Document read lease blocks concurrent mutation",
          "[document][threading]")
{
    cupuacu::Document document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 4);
    document.setSample(0, 0, 0.0f);

    std::future<void> mutation;
    {
        const auto lease = document.acquireReadLease();
        mutation = std::async(std::launch::async,
                              [&document]
                              {
                                  document.setSample(0, 0, 0.5f);
                              });

        REQUIRE(mutation.wait_for(std::chrono::milliseconds(20)) ==
                std::future_status::timeout);
    }

    mutation.get();
    REQUIRE(document.getSample(0, 0) == 0.5f);
}

TEST_CASE("Document marker edits preserve identity", "[document][markers]")
{
    cupuacu::Document document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 100);

    const uint64_t markerId = document.addMarker(20, "Verse");

    REQUIRE(document.setMarkerFrame(markerId, 44));
    REQUIRE(document.setMarkerLabel(markerId, "Hook"));
    REQUIRE_FALSE(document.setMarkerFrame(9999, 12));
    REQUIRE_FALSE(document.setMarkerLabel(9999, "Missing"));

    REQUIRE(document.getMarkers().size() == 1);
    REQUIRE(document.getMarkers()[0] ==
            cupuacu::DocumentMarker{
                .id = markerId,
                .frame = 44,
                .label = "Hook",
            });
}

TEST_CASE("Document markers follow insert and remove frame edits",
          "[document][markers]")
{
    cupuacu::Document document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 100);

    const uint64_t earlyId = document.addMarker(10, "A");
    const uint64_t middleId = document.addMarker(40, "B");
    const uint64_t lateId = document.addMarker(90, "C");

    document.insertFrames(30, 5);

    REQUIRE(document.getMarkers()[0].frame == 10);
    REQUIRE(document.getMarkers()[1].frame == 45);
    REQUIRE(document.getMarkers()[2].frame == 95);

    document.removeFrames(20, 30);

    REQUIRE(document.getMarkers().size() == 3);
    REQUIRE(document.getMarkers()[0] ==
            cupuacu::DocumentMarker{
                .id = earlyId,
                .frame = 10,
                .label = "A",
            });
    REQUIRE(document.getMarkers()[1] ==
            cupuacu::DocumentMarker{
                .id = middleId,
                .frame = 20,
                .label = "B",
            });
    REQUIRE(document.getMarkers()[2] ==
            cupuacu::DocumentMarker{
                .id = lateId,
                .frame = 65,
                .label = "C",
            });
}

TEST_CASE("Document remove progress stays monotonic across internal phases",
          "[document][progress]")
{
    cupuacu::Document document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 2, 100);

    std::vector<std::pair<int64_t, int64_t>> updates;
    document.removeFrames(
        20, 30,
        [&](const int64_t completed, const int64_t total)
        {
            updates.emplace_back(completed, total);
        });

    REQUIRE(updates.size() >= 2);
    const auto expectedTotal = updates.front().second;
    REQUIRE(expectedTotal > 0);

    int64_t previousCompleted = 0;
    for (const auto &[completed, total] : updates)
    {
        REQUIRE(total == expectedTotal);
        REQUIRE(completed >= previousCompleted);
        previousCompleted = completed;
    }

    REQUIRE(updates.back().first == expectedTotal);
}

TEST_CASE("Audio buffer remove progress reports within a large channel move",
          "[document][progress]")
{
    cupuacu::audio::AudioBuffer buffer;
    buffer.resize(1, 400000);

    std::vector<std::pair<int64_t, int64_t>> updates;
    buffer.removeFrames(
        1000, 1000,
        [&](const int64_t completed, const int64_t total)
        {
            updates.emplace_back(completed, total);
        });

    REQUIRE(updates.size() > 2);
    const auto expectedTotal = updates.front().second;
    REQUIRE(expectedTotal > 1);

    int64_t previousCompleted = 0;
    for (const auto &[completed, total] : updates)
    {
        REQUIRE(total == expectedTotal);
        REQUIRE(completed >= previousCompleted);
        previousCompleted = completed;
    }

    REQUIRE(updates.back().first == expectedTotal);
}

TEST_CASE("Document remove progress advances during the initial sample shift",
          "[document][progress]")
{
    cupuacu::Document document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 2, 400000);

    std::vector<std::pair<int64_t, int64_t>> updates;
    document.removeFrames(
        1000, 1000,
        [&](const int64_t completed, const int64_t total)
        {
            updates.emplace_back(completed, total);
        });

    REQUIRE(updates.size() > 3);
    REQUIRE(updates[0].second == updates[1].second);
    REQUIRE(updates[1].first > updates[0].first);
}

TEST_CASE("Replacing document markers normalizes ids and clears on initialize",
          "[document][markers]")
{
    cupuacu::Document document;
    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 50);

    document.replaceMarkers({
        cupuacu::DocumentMarker{.id = 0, .frame = -10, .label = "Start"},
        cupuacu::DocumentMarker{.id = 42, .frame = 80, .label = "End"},
    });

    REQUIRE(document.getMarkers().size() == 2);
    REQUIRE(document.getMarkers()[0].id == 1);
    REQUIRE(document.getMarkers()[0].frame == 0);
    REQUIRE(document.getMarkers()[1].id == 42);
    REQUIRE(document.getMarkers()[1].frame == 50);

    const uint64_t nextId = document.addMarker(25, "Middle");
    REQUIRE(nextId == 43);

    document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 10);
    REQUIRE(document.getMarkers().empty());
    REQUIRE(document.addMarker(3, "New") == 1);
}

TEST_CASE("Document writes interleaved float blocks with one waveform version bump",
          "[document][samples]")
{
    cupuacu::Document document;
    document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2, 4);

    const auto versionBeforeWrite = document.getWaveformDataVersion();
    const float interleaved[] = {
        0.25f, -0.25f,
        0.5f, -0.5f,
        0.75f, -0.75f,
    };

    document.writeInterleavedFloatBlock(1, interleaved, 3, 2, false);

    REQUIRE(document.getSample(0, 0) == 0.0f);
    REQUIRE(document.getSample(1, 0) == 0.0f);
    REQUIRE(document.getSample(0, 1) == 0.25f);
    REQUIRE(document.getSample(1, 1) == -0.25f);
    REQUIRE(document.getSample(0, 3) == 0.75f);
    REQUIRE(document.getSample(1, 3) == -0.75f);
    REQUIRE(document.getWaveformDataVersion() == versionBeforeWrite + 1);
}
