#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestPaths.hpp"
#include "TestResourceUtil.hpp"
#include "actions/audio/EditCommands.hpp"
#include "file/file_loading.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/LabeledField.hpp"
#include "gui/Waveform.hpp"
#include "gui/WaveformBlockRenderPlanning.hpp"
#include "gui/WaveformOverviewPlanning.hpp"
#include "gui/ScrollBar.hpp"
#include "gui/WaveformSmoothRenderPlanning.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <filesystem>
#include <chrono>
#include <system_error>
#include <thread>
#include <vector>

namespace
{
    cupuacu::gui::MouseEvent leftMouseDownAt(const int x, const int y)
    {
        return cupuacu::gui::MouseEvent{
            cupuacu::gui::DOWN, x, y, static_cast<float>(x),
            static_cast<float>(y), 0.0f, 0.0f,
            cupuacu::gui::MouseButtonState{true, false, false}, 1};
    }

    cupuacu::gui::MouseEvent leftMouseMoveAt(const int x, const int y)
    {
        return cupuacu::gui::MouseEvent{
            cupuacu::gui::MOVE, x, y, static_cast<float>(x),
            static_cast<float>(y), 0.0f, 0.0f,
            cupuacu::gui::MouseButtonState{true, false, false}, 0};
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

    std::vector<cupuacu::gui::BlockWaveformPeakColumnPlan>
    planBlockPeaks(const cupuacu::DocumentSession &session,
                   const int channelIndex,
                   const int64_t sampleOffset,
                   const double samplesPerPixel,
                   const int widthToUse,
                   const uint8_t pixelScale)
    {
        return cupuacu::gui::planWaveformOverviewPeakColumns(
            session, channelIndex, sampleOffset, samplesPerPixel, widthToUse,
            pixelScale);
    }
} // namespace

TEST_CASE("Waveform smooth render planning derives interpolation input safely",
          "[gui]")
{
    const auto empty = cupuacu::gui::planWaveformSmoothRenderInput(
        0, 1.0, 0, 0.5, 8, [](const int64_t) { return 0.0f; });
    REQUIRE(empty.sampleX.empty());
    REQUIRE(empty.sampleY.empty());
    REQUIRE(empty.queryX.empty());

    const auto input = cupuacu::gui::planWaveformSmoothRenderInput(
        3, 1.0, 2, 0.5, 10,
        [](const int64_t sampleIndex)
        {
            return static_cast<float>(sampleIndex) / 10.0f;
        });

    REQUIRE(input.sampleX.size() == 6);
    REQUIRE(input.sampleY.size() == 6);
    REQUIRE(input.queryX.size() == 4);

    REQUIRE(input.sampleX.front() == Catch::Approx(-0.5));
    REQUIRE(input.sampleX.back() == Catch::Approx(4.5));
    REQUIRE(input.sampleY.front() == Catch::Approx(0.1f));
    REQUIRE(input.sampleY.back() == Catch::Approx(0.6f));
    REQUIRE(input.queryX.front() == Catch::Approx(0.0));
    REQUIRE(input.queryX.back() == Catch::Approx(3.0));
}

TEST_CASE("Waveform overview planning maps frame spans to fill rects",
          "[gui]")
{
    const auto rect = cupuacu::gui::planFrameSpanRect(20, 40, 0, 2.0, 100, 24);
    REQUIRE(rect.has_value());
    REQUIRE(rect->x >= 0);
    REQUIRE(rect->w > 0);
    REQUIRE(rect->h == 24);
}

TEST_CASE("Waveform smooth spline evaluation and segment quads handle edge cases",
          "[gui]")
{
    REQUIRE(cupuacu::gui::evaluateWaveformSmoothSpline({}).empty());

    const cupuacu::gui::WaveformSmoothRenderInput line{
        {0.0, 1.0},
        {0.0, 1.0},
        {0.0, 0.5, 1.0}};
    const auto smoothed = cupuacu::gui::evaluateWaveformSmoothSpline(line);
    REQUIRE(smoothed.size() == 3);
    REQUIRE(smoothed.front() == Catch::Approx(0.0f));
    REQUIRE(smoothed.back() == Catch::Approx(1.0f));

    REQUIRE_FALSE(cupuacu::gui::planWaveformSmoothSegmentQuad(
                      1.0f, 1.0f, 2.0f, 2.0f, 3.0f)
                      .has_value());

    const auto horizontal = cupuacu::gui::planWaveformSmoothSegmentQuad(
        0.0f, 10.0f, 5.0f, 5.0f, 4.0f);
    REQUIRE(horizontal.has_value());
    REQUIRE(horizontal->vertices[0].x == Catch::Approx(0.0f));
    REQUIRE(horizontal->vertices[0].y == Catch::Approx(3.0f));
    REQUIRE(horizontal->vertices[1].y == Catch::Approx(7.0f));

    const auto vertical = cupuacu::gui::planWaveformSmoothSegmentQuad(
        8.0f, 8.0f, 1.0f, 9.0f, 2.0f);
    REQUIRE(vertical.has_value());
    REQUIRE(vertical->vertices[0].x == Catch::Approx(9.0f));
    REQUIRE(vertical->vertices[1].x == Catch::Approx(7.0f));
    REQUIRE(vertical->vertices[0].y == Catch::Approx(1.0f));
    REQUIRE(vertical->vertices[2].y == Catch::Approx(9.0f));
}

TEST_CASE("LabeledField marks itself dirty only when the displayed value changes",
          "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    cupuacu::gui::LabeledField field(&state, "Pos", SDL_Color{0, 0, 0, 255});
    field.setVisible(true);

    REQUIRE_FALSE(field.isDirty());
    field.setValue("");
    REQUIRE_FALSE(field.isDirty());

    field.setValue("42");
    REQUIRE(field.isDirty());
}

TEST_CASE("Waveform tolerates a temporary channel mismatch during transitions",
          "[gui][waveform]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().document.initialize(
        cupuacu::SampleFormat::Unknown, 0, 0, 0);
    state.getActiveViewState().samplesPerPixel = 0.5;

    cupuacu::gui::Waveform waveform(&state, 0);
    waveform.setVisible(true);
    waveform.setBounds(0, 0, 200, 80);

    REQUIRE_NOTHROW(waveform.updateSamplePoints());
    REQUIRE(waveform.getChildren().empty());
}

TEST_CASE("In-place waveform rebuild reads only dirty blocks", "[waveform]")
{
    std::vector<float> samples(1025);
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        samples[i] = float(i % 19) / 32.0f;
    }
    cupuacu::gui::WaveformCache cache;
    cache.rebuildAll(samples.data(), samples.size());
    int64_t first = 0, end = 0;
    SECTION("clean cache reads no samples") {}
    SECTION("interior block")
    {
        samples[130] = -0.9f;
        cache.invalidateSample(130);
        first = 128;
        end = 256;
    }
    SECTION("partial final block")
    {
        samples[1024] = -0.9f;
        cache.invalidateSample(1024);
        first = 1024;
        end = 1025;
    }
    struct Reader
    {
        const std::vector<float> &samples;
        int64_t first, end;
        mutable int64_t reads = 0;
        float operator[](int64_t i) const
        {
            REQUIRE(i >= first);
            REQUIRE(i < end);
            ++reads;
            return samples.at(static_cast<std::size_t>(i));
        }
    } reader{samples, first, end};
    cache.rebuildDirtyFrom(reader);
    REQUIRE(reader.reads == end - first);
    REQUIRE_FALSE(cache.hasDirtyBlocks());
    cupuacu::gui::WaveformCache expected;
    expected.rebuildAll(samples.data(), samples.size());
    for (int level = 0; level < expected.levelsCount(); ++level)
    {
        const auto &a = cache.getLevelByIndex(level);
        const auto &b = expected.getLevelByIndex(level);
        REQUIRE(a.size() == b.size());
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            REQUIRE(a[i].min == b[i].min);
            REQUIRE(a[i].max == b[i].max);
        }
    }
}

TEST_CASE("DocumentSession publishes waveform cache results from background work",
          "[gui][waveform]")
{
    cupuacu::DocumentSession session;
    auto &document = session.document;
    document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, 256);
    for (int64_t frame = 0; frame < 256; ++frame)
    {
        document.setSample(0, frame,
                           (frame % 32) < 16 ? -0.5f : 0.5f, false);
    }

    session.invalidateWaveformSamples(0, 255);
    session.updateWaveformCache();

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    bool built = false;
    while (std::chrono::steady_clock::now() < deadline)
    {
        (void)session.pumpWaveformCacheWork();
        const auto &cache = session.getWaveformCache(0);
        if (cache.levelsCount() > 0 && !cache.hasDirtyBlocks())
        {
            built = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    REQUIRE(built);
}

TEST_CASE("Waveform overview planning keeps drawing the clean prefix after frame erasure while cache rebuild is pending",
          "[gui][waveform]")
{
    cupuacu::DocumentSession session;
    auto &document = session.document;
    const int64_t frameCount = 4096;
    document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, frameCount);
    for (int64_t frame = 0; frame < frameCount; ++frame)
    {
        const float value = (frame % 64) < 32 ? -0.75f : 0.75f;
        document.setSample(0, frame, value, false);
    }

    session.invalidateWaveformSamples(0, frameCount - 1);
    session.updateWaveformCache();

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    bool built = false;
    while (std::chrono::steady_clock::now() < deadline)
    {
        (void)session.pumpWaveformCacheWork();
        const auto &cache = session.getWaveformCache(0);
        if (cache.levelsCount() > 0 && !cache.hasDirtyBlocks())
        {
            built = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(built);

    document.removeFrames(1024, 512);

    cupuacu::gui::Peak peak{};
    const bool hasPeak = cupuacu::gui::computeWaveformPeakForSampleWindow(
        session, 0, 0, 64.0, 1, 768.0, 832.0, peak);

    REQUIRE(hasPeak);
    REQUIRE(peak.min < 0.0f);
    REQUIRE(peak.max > 0.0f);
}

TEST_CASE("DocumentSession reports deterministic waveform cache build progress",
          "[gui][waveform]")
{
    cupuacu::DocumentSession session;
    auto &document = session.document;
    constexpr int64_t frameCount = 1 << 22;
    document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, frameCount);
    for (int64_t frame = 0; frame < frameCount; ++frame)
    {
        document.setSample(0, frame,
                           (frame % 64) < 32 ? -0.75f : 0.75f, false);
    }

    session.invalidateWaveformSamples(0, frameCount - 1);
    session.updateWaveformCache();

    const auto initialProgress = session.getWaveformCacheBuildProgress();
    REQUIRE(initialProgress.has_value());
    REQUIRE(initialProgress->completedBlocks == 0);
    REQUIRE(initialProgress->totalBlocks > 0);

    std::vector<double> seenProgress{0.0};
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (const auto progress = session.getWaveformCacheBuildProgress();
            progress.has_value() && progress->totalBlocks > 0)
        {
            seenProgress.push_back(
                static_cast<double>(progress->completedBlocks) /
                static_cast<double>(progress->totalBlocks));
        }

        (void)session.pumpWaveformCacheWork();
        if (!session.getWaveformCacheBuildProgress().has_value())
        {
            seenProgress.push_back(1.0);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    REQUIRE_FALSE(session.getWaveformCacheBuildProgress().has_value());
    REQUIRE(seenProgress.size() >= 2);
    REQUIRE(std::is_sorted(seenProgress.begin(), seenProgress.end()));
    REQUIRE(seenProgress.front() == Catch::Approx(0.0));
    REQUIRE(seenProgress.back() == Catch::Approx(1.0));
}

TEST_CASE("DocumentSession reports waveform cache progress from applied chunks",
          "[gui][waveform]")
{
    cupuacu::DocumentSession session;
    auto &document = session.document;
    constexpr int64_t frameCount = 1 << 22;
    document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, frameCount);
    for (int64_t frame = 0; frame < frameCount; ++frame)
    {
        document.setSample(0, frame,
                           (frame % 64) < 32 ? -0.5f : 0.5f, false);
    }

    session.invalidateWaveformSamples(0, frameCount - 1);
    session.updateWaveformCache();

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    const auto beforePump = session.getWaveformCacheBuildProgress();
    REQUIRE(beforePump.has_value());
    REQUIRE(beforePump->completedBlocks == 0);
    REQUIRE(beforePump->totalBlocks > 0);

    bool observedAppliedAdvance = false;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (session.pumpWaveformCacheWork())
        {
            observedAppliedAdvance = true;
        }
        if (const auto progress = session.getWaveformCacheBuildProgress();
            progress.has_value() && progress->completedBlocks > 0)
        {
            observedAppliedAdvance = true;
        }
        if (observedAppliedAdvance)
        {
            break;
        }
        if (!session.getWaveformCacheBuildProgress().has_value())
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    REQUIRE(observedAppliedAdvance);
}

TEST_CASE("Overview rendering falls back to raw samples past the dirty cache frontier",
          "[gui][waveform]")
{
    cupuacu::DocumentSession session;
    auto &document = session.document;
    constexpr int64_t frameCount = 32768;
    document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, frameCount);
    for (int64_t frame = 0; frame < frameCount; ++frame)
    {
        document.setSample(0, frame, frame < (frameCount / 2) ? 0.25f : -0.75f,
                           false);
    }

    session.rebuildWaveformCacheSynchronously();
    session.invalidateWaveformSamples(frameCount / 2, frameCount - 1);

    cupuacu::gui::Peak peak{};
    REQUIRE(cupuacu::gui::computeWaveformPeakForSampleWindow(
        session, 0, 0, 1024.0, 1, 15 * 1024.0, 16 * 1024.0, peak));
    REQUIRE(peak.min == Catch::Approx(0.25f));
    REQUIRE(peak.max == Catch::Approx(0.25f));

    REQUIRE(cupuacu::gui::computeWaveformPeakForSampleWindow(
        session, 0, 0, 1024.0, 1, 20 * 1024.0, 21 * 1024.0, peak));
    REQUIRE(peak.min == Catch::Approx(-0.75f));
    REQUIRE(peak.max == Catch::Approx(-0.75f));

    cupuacu::gui::WaveformOverviewDebugStats stats{};
    REQUIRE(cupuacu::gui::computeWaveformPeakForSampleWindow(
        session, 0, 0, 2.0, 1, 16383.0, 16385.0, peak, &stats));
    REQUIRE(peak.min == -0.75f);
    REQUIRE(peak.max == 0.25f);
    REQUIRE(cupuacu::gui::computeWaveformPeakForSampleWindow(
        session, 0, 0, 0.5, 1, 16384.0, 16384.5, peak, &stats));
    REQUIRE(peak.min == -0.75f);
    REQUIRE(peak.max == -0.75f);
    REQUIRE(stats.rawSamplesScanned == 3);
    REQUIRE(stats.windowsBypassedCache == 2);
}

TEST_CASE("Progressive overview rendering only requests built audio",
          "[gui][waveform]")
{
    using cupuacu::gui::planWaveformRenderFrameLimit;
    cupuacu::gui::WaveformCache cache;
    constexpr int64_t frames = 32768;
    REQUIRE(planWaveformRenderFrameLimit(frames, 4096, 1, cache, true) == 0);
    cache.init(frames);
    REQUIRE(planWaveformRenderFrameLimit(frames, 4096, 1, cache, true) == 0);
    std::vector<float> samples(frames, 0.5f);
    cache.rebuildAll(samples.data(), frames);
    cache.invalidateSamples(128 * 37, frames - 1);
    REQUIRE(planWaveformRenderFrameLimit(frames, 4096, 1, cache, true) ==
            128 * 37);
    REQUIRE(planWaveformRenderFrameLimit(frames, 4096, 1, cache, false) ==
            frames);
    REQUIRE(planWaveformRenderFrameLimit(frames, 1, 1, cache, true) == frames);
    REQUIRE(planWaveformRenderFrameLimit(frames, 128, 2, cache, true) ==
            frames);
    REQUIRE(planWaveformRenderFrameLimit(frames, 256, 2, cache, true) ==
            128 * 37);
    cache.rebuildDirty(samples.data());
    REQUIRE(planWaveformRenderFrameLimit(frames, 4096, 1, cache, true) ==
            frames);
}

TEST_CASE("Zoomed-out exact peak queries bound raw scans at cache boundaries",
          "[gui][waveform]")
{
    cupuacu::DocumentSession session;
    constexpr int64_t frames = 262147;
    std::vector<float> samples(frames);
    for (int64_t i = 0; i < frames; ++i)
    {
        samples[i] = float((i * 37) % 1009 - 504) / 512.0f;
    }
    session.document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1,
                                frames);
    session.document.writeInterleavedFloatBlock(0, samples.data(), frames, 1,
                                                false);
    session.rebuildWaveformCacheSynchronously();

    for (const double zoom : {128.0, 1024.0, 16384.0, 131072.0})
    {
        for (const int64_t start : {0, 1, 127, 128, 333, 65535, 262140})
        {
            for (const int64_t length : {1, 127, 128, 129, 1931, 98309})
            {
                const auto end = std::min(frames, start + length);
                cupuacu::gui::Peak peak{};
                cupuacu::gui::WaveformOverviewDebugStats stats{};
                REQUIRE(cupuacu::gui::computeWaveformPeakForSampleWindow(
                    session, 0, 0, zoom, 1, start, end, peak, &stats));
                const auto expected = std::minmax_element(
                    samples.begin() + start, samples.begin() + end);
                REQUIRE(peak.min == *expected.first);
                REQUIRE(peak.max == *expected.second);
                REQUIRE(stats.rawSamplesScanned < 256);
            }
        }
    }

    // The top level covering this window is incomplete; its lower-level
    // children before the dirty frontier are still usable.
    session.invalidateWaveformSamples(100 * 128, frames - 1);
    cupuacu::gui::Peak peak{};
    cupuacu::gui::WaveformOverviewDebugStats stats{};
    REQUIRE(cupuacu::gui::computeWaveformPeakForSampleWindow(
        session, 0, 0, 131072, 1, 1, 100 * 128, peak, &stats));
    const auto expected =
        std::minmax_element(samples.begin() + 1, samples.begin() + 100 * 128);
    REQUIRE(peak.min == *expected.first);
    REQUIRE(peak.max == *expected.second);
    REQUIRE(stats.rawSamplesScanned == 127);
}

TEST_CASE("Background block render input avoids raw sample snapshots for zoomed-out views",
          "[gui][waveform]")
{
    constexpr int64_t frameCount = 262144;
    cupuacu::gui::WaveformCache cache;
    cache.init(frameCount);

    const auto zoomedOut = cupuacu::gui::planBackgroundBlockRenderInput(
        frameCount, 0, 200000.0, 2000, 1, cache);
    REQUIRE_FALSE(zoomedOut.bypassCache);
    REQUIRE(zoomedOut.samplesPerPeak > 0);
    REQUIRE(zoomedOut.rawSampleStart == 0);
    REQUIRE(zoomedOut.rawSampleEndExclusive == 0);
    REQUIRE(cache.getLevelByIndex(zoomedOut.cacheLevel).size() < 10000);

    const auto zoomedIn = cupuacu::gui::planBackgroundBlockRenderInput(
        frameCount, 128, 2.0, 2000, 1, cache);
    REQUIRE(zoomedIn.bypassCache);
    REQUIRE(zoomedIn.rawSampleStart == 128);
    REQUIRE(zoomedIn.rawSampleEndExclusive == 4130);
    REQUIRE(zoomedIn.samplesPerPeak == 0);
}

TEST_CASE("ScrollBar vertical drag updates value and non-left clicks are ignored",
          "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    double value = 25.0;

    cupuacu::gui::ScrollBar bar(
        &state, cupuacu::gui::ScrollBar::Orientation::Vertical,
        [&]() { return value; },
        []() { return 0.0; },
        []() { return 100.0; },
        []() { return 20.0; },
        [&](const double next) { value = next; });
    bar.setVisible(true);
    bar.setBounds(0, 0, 12, 120);

    REQUIRE_FALSE(bar.mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN, 6, 20, 6.0f, 20.0f, 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{false, true, false}, 1}));

    REQUIRE(bar.mouseDown(leftMouseDownAt(6, 105)));
    REQUIRE(value > 70.0);
    REQUIRE(value <= 100.0);

    REQUIRE(bar.mouseMove(leftMouseMoveAt(6, 200)));
    REQUIRE(value == Catch::Approx(100.0));

    REQUIRE(bar.mouseUp(cupuacu::gui::MouseEvent{
        cupuacu::gui::UP, 6, 200, 6.0f, 200.0f, 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{true, false, false}, 1}));
}

TEST_CASE("Block waveform overview preserves pasted-copy peaks in the former comb region",
          "[gui][waveform]")
{
    ScopedDirCleanup cleanup(cupuacu::test::makeUniqueTestRoot(
        "waveform-render-finger-cym1"));
    const auto wavPath = cleanup.path() / "FINGER_CYM1.WAV";
    cupuacu::test::write_test_resource_file("FINGER_CYM1.WAV", wavPath);

    cupuacu::test::StateWithTestPaths originalState(cleanup.path() / "original");
    auto &originalSession = originalState.getActiveDocumentSession();
    originalSession.currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&originalState);
    originalSession.rebuildWaveformCacheSynchronously();

    cupuacu::test::StateWithTestPaths pastedState(cleanup.path() / "pasted");
    auto &pastedSession = pastedState.getActiveDocumentSession();
    pastedSession.currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&pastedState);
    pastedSession.rebuildWaveformCacheSynchronously();

    const int64_t pasteOffset = 42197;
    pastedSession.selection.setValue1(0.0);
    pastedSession.selection.setValue2(
        static_cast<double>(pastedSession.document.getFrameCount()));
    cupuacu::actions::audio::performCopy(&pastedState);
    pastedSession.selection.reset();
    pastedSession.cursor = pasteOffset;
    cupuacu::actions::audio::performPaste(&pastedState);
    pastedSession.rebuildWaveformCacheSynchronously();

    for (const int totalWidth : {756, 1200})
    {
        const int64_t formerCombProbeSamplesInCopy[] = {14091, 34607};
        const double samplesPerPixel =
            static_cast<double>(pastedSession.document.getFrameCount()) /
            static_cast<double>(totalWidth);
        const int copyWidth = static_cast<int>(std::ceil(
            static_cast<double>(originalSession.document.getFrameCount()) /
            samplesPerPixel));

        const auto originalColumns =
            planBlockPeaks(originalSession, 0, 0, samplesPerPixel,
                           copyWidth, pastedState.pixelScale);
        const auto pastedColumns =
            planBlockPeaks(pastedSession, 0, pasteOffset,
                           samplesPerPixel, copyWidth, pastedState.pixelScale);

        std::map<int, cupuacu::gui::Peak> originalByDrawXi;
        std::map<int, cupuacu::gui::Peak> pastedByDrawXi;
        for (const auto &column : originalColumns)
        {
            originalByDrawXi[column.drawXi] = column.peak;
        }
        for (const auto &column : pastedColumns)
        {
            pastedByDrawXi[column.drawXi] = column.peak;
        }

        REQUIRE_FALSE(originalByDrawXi.empty());
        REQUIRE_FALSE(pastedByDrawXi.empty());

        for (const int64_t probeSampleInCopy : formerCombProbeSamplesInCopy)
        {
            const int probeDrawXi = static_cast<int>(std::floor(
                static_cast<double>(probeSampleInCopy) / samplesPerPixel));
            REQUIRE(originalByDrawXi.find(probeDrawXi) != originalByDrawXi.end());
            REQUIRE(pastedByDrawXi.find(probeDrawXi) != pastedByDrawXi.end());
        }

        for (const auto &[drawXi, peak] : originalByDrawXi)
        {
            const auto pastedIt = pastedByDrawXi.find(drawXi);
            REQUIRE(pastedIt != pastedByDrawXi.end());
            REQUIRE(peak.min == Catch::Approx(pastedIt->second.min));
            REQUIRE(peak.max == Catch::Approx(pastedIt->second.max));
        }
    }
}

TEST_CASE("Peak pages detach only where a snapshot is edited", "[waveform]")
{
    using cupuacu::gui::PeakLevel;
    static_assert(std::random_access_iterator<PeakLevel::ConstIterator>);
    PeakLevel original;
    original.resize(PeakLevel::PEAKS_PER_PAGE * 3 + 7);
    for (std::size_t i = 0; i < original.size(); ++i)
    {
        original.set(i, {-float(i), float(i)});
    }
    auto edited = original;
    REQUIRE(&edited[0] == &original[0]);
    REQUIRE(&edited[PeakLevel::PEAKS_PER_PAGE] == &original[PeakLevel::PEAKS_PER_PAGE]);
    edited.set(PeakLevel::PEAKS_PER_PAGE - 1, {-9, 9});
    REQUIRE(&edited[0] != &original[0]);
    REQUIRE(&edited[PeakLevel::PEAKS_PER_PAGE] == &original[PeakLevel::PEAKS_PER_PAGE]);
    REQUIRE(original[PeakLevel::PEAKS_PER_PAGE - 1].max ==
            float(PeakLevel::PEAKS_PER_PAGE - 1));
    auto newer = edited;
    edited.set(PeakLevel::PEAKS_PER_PAGE, {-8, 8});
    REQUIRE(newer[PeakLevel::PEAKS_PER_PAGE].max == float(PeakLevel::PEAKS_PER_PAGE));
    original.resize(0);
    REQUIRE(newer[PeakLevel::PEAKS_PER_PAGE - 1].max == 9);
    const std::vector<cupuacu::gui::Peak> range(newer.begin() + 1020,
                                                newer.begin() + 1030);
    REQUIRE(range.size() == 10);
    REQUIRE(range[3].max == 9);
    REQUIRE(range[4].max == 1024);
    PeakLevel moved = std::move(newer);
    REQUIRE(newer.empty());
    newer.resize(2);
    REQUIRE(newer[0].min == 0);
    REQUIRE(moved[1023].max == 9);
}

TEST_CASE("Peak page resizing matches a contiguous array across snapshots",
          "[waveform]")
{
    using cupuacu::gui::Peak;
    using cupuacu::gui::PeakLevel;
    PeakLevel level;
    std::vector<Peak> expected;
    // Includes shrinking inside a shared page, growing it again, and crossing
    // page boundaries. Truncated values must not reappear when growing.
    for (const std::size_t size : {3079, 1025, 2048, 7, 9, 1024, 1031, 0, 2049})
    {
        const auto snapshot = level;
        const auto saved = expected;
        level.resize(size);
        expected.resize(size);
        for (std::size_t i = 0; i < size; ++i)
        {
            REQUIRE(level[i].min == expected[i].min);
            REQUIRE(level[i].max == expected[i].max);
            level.set(i, {-float(i + size), float(i + size)});
            expected[i] = {-float(i + size), float(i + size)};
        }
        REQUIRE(snapshot.size() == saved.size());
        for (std::size_t i = 0; i < saved.size(); ++i)
        {
            REQUIRE(snapshot[i].min == saved[i].min);
            REQUIRE(snapshot[i].max == saved[i].max);
        }
    }
}

TEST_CASE("Concurrent cache revisions cannot overwrite shared peaks",
          "[waveform]")
{
    cupuacu::gui::PeakLevel original;
    original.resize(2051);
    for (std::size_t i = 0; i < original.size(); ++i)
    {
        original.set(i, {-1, 1});
    }
    auto first = original;
    auto second = original;
    std::thread a(
        [&]
        {
            for (std::size_t i = 0; i < first.size(); ++i)
            {
                first.set(i, {-2, 2});
            }
        });
    std::thread b(
        [&]
        {
            for (std::size_t i = 0; i < second.size(); ++i)
            {
                second.set(i, {-3, 3});
            }
        });
    a.join();
    b.join();
    for (std::size_t i = 0; i < original.size(); ++i)
    {
        REQUIRE(original[i].max == 1);
        REQUIRE(first[i].max == 2);
        REQUIRE(second[i].max == 3);
    }
}

TEST_CASE(
    "Structural waveform edits preserve snapshots and rebuild dirty suffixes",
    "[waveform]")
{
    std::vector<float> samples(cupuacu::gui::PeakLevel::PEAKS_PER_PAGE * 128 + 137);
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        samples[i] = float(i % 31) / 32;
    }
    cupuacu::gui::WaveformCache cache;
    cache.rebuildAll(samples.data(), samples.size());
    const auto original = cache.snapshotBuildState();
    const auto saved = samples;
    SECTION("Insert within a block")
    {
        samples.insert(samples.begin() + 127, 193, -1.0f);
        cache.applyInsert(127, 193);
    }
    SECTION("Erase across a page boundary")
    {
        samples.erase(samples.begin() + 131000, samples.begin() + 131173);
        cache.applyErase(131000, 131173);
    }
    SECTION("Append to a partial last block")
    {
        cache.applyInsert(samples.size(), 193);
        samples.insert(samples.end(), 193, -1.0f);
    }
    SECTION("Erase everything")
    {
        cache.applyErase(0, samples.size());
        samples.clear();
    }
    cache.rebuildDirty(samples.data());
    cupuacu::gui::WaveformCache expected;
    expected.rebuildAll(samples.data(), samples.size());
    const auto actual = cache.snapshotBuildState();
    // Erasing all samples may retain empty upper levels; compare every level
    // that the fresh rebuild uses and verify no remaining dirty work.
    REQUIRE_FALSE(cache.hasDirtyBlocks());
    for (int level = 0; level < expected.levelsCount(); ++level)
    {
        const auto &a = actual.levels[level];
        const auto &b = expected.getLevelByIndex(level);
        REQUIRE(a.size() == b.size());
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            REQUIRE(a[i].min == b[i].min);
            REQUIRE(a[i].max == b[i].max);
        }
    }
    for (std::size_t block = 0; block < original.levels[0].size(); ++block)
    {
        const auto begin = saved.begin() + block * 128;
        const auto end =
            saved.begin() + std::min(saved.size(), (block + 1) * 128);
        REQUIRE(original.levels[0][block].min == *std::min_element(begin, end));
        REQUIRE(original.levels[0][block].max == *std::max_element(begin, end));
    }
}
