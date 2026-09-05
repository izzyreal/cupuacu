#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "TestPaths.hpp"
#include "State.hpp"
#include "actions/audio/EditCommands.hpp"
#include "actions/effects/RevisionEffect.hpp"
#include "effects/MakeSilentEffect.hpp"
#include "waveform/DecodedWaveformBuilder.hpp"
#include <random>

using namespace cupuacu;
namespace
{
    struct Fixture
    {
        State state;
        std::shared_ptr<storage::AudioBlockStore> store =
            std::make_shared<storage::AudioBlockStore>(
                test::makeUniqueTestRoot("revision-commands") / "audio");
        std::shared_ptr<storage::DecodedBlockCache> cache =
            std::make_shared<storage::DecodedBlockCache>(
                storage::AudioBlockBytes);
        std::shared_ptr<const storage::AudioEditRevision> original;
        std::vector<float> samples;
        Fixture(int64_t frames = 65573)
        {
            storage::AudioShape shape{frames, 2, 48000, SampleFormat::FLOAT32};
            storage::AudioRevisionBuilder builder(shape, store, cache);
            samples.resize(frames * 2);
            for (int64_t i = 0; i < frames; ++i)
            {
                for (int c = 0; c < 2; ++c)
                {
                    samples[i * 2 + c] =
                        (i % 997 < 40) ? 0.f : float((i % 71) - 35 + c) / 64.f;
                }
            }
            builder.appendInterleaved(samples);
            waveform::DecodedWaveformBuilder peaks;
            peaks.appendFrom(shape, frames,
                             [&](int c, int64_t first, std::span<float> out)
                             {
                                 for (std::size_t i = 0; i < out.size(); ++i)
                                 {
                                     out[i] = samples[(first + i) * 2 + c];
                                 }
                             });
            auto caches = peaks.takeCaches();
            std::vector<std::vector<gui::PeakLevel>> levels;
            for (int c = 0; c < 2; ++c)
            {
                levels.push_back(
                    caches.getCache(c).snapshotBuildState().levels);
            }
            original = storage::AudioEditRevision::from(
                builder.finish({}, std::make_shared<waveform::SourcePeaks>(
                                       shape, std::move(levels))));
            auto &session = state.getActiveDocumentSession();
            session.document.setExternalAudioShape(
                shape.format, shape.sampleRate, shape.channels, shape.frames);
            session.bindReadRevision(original);
        }
        std::vector<float> read() const
        {
            auto reader = state.getActiveDocumentSession().getAudioReader();
            auto shape = reader->shape();
            std::vector<float> result(shape.frames * shape.channels),
                channel(shape.frames);
            for (int c = 0; c < shape.channels; ++c)
            {
                reader->readChannel(c, 0, channel);
                for (int64_t i = 0; i < shape.frames; ++i)
                {
                    result[i * shape.channels + c] = channel[i];
                }
            }
            return result;
        }
        void select(int64_t start, int64_t count)
        {
            auto &selection = state.getActiveDocumentSession().selection;
            selection.reset();
            selection.setValue1(start);
            selection.setValue2(start + count);
        }
    };
} // namespace

TEST_CASE("Production reference commands share audio and restore editor state",
          "[revision-commands]")
{
    Fixture f;
    auto &session = f.state.getActiveDocumentSession();
    session.document.addMarker(65531, "inside cut");
    session.document.addMarker(65571, "after cut");
    auto markers = session.document.getMarkers();
    f.select(65530, 29);
    session.cursor = 65560;
    const auto io = f.store->ioBytes();
    actions::audio::performCut(&f.state);
    REQUIRE(f.store->ioBytes() == io);
    REQUIRE(f.state.clipboard.getFrameCount() == 29);
    REQUIRE(f.state.clipboard.getAudioRevision());
    REQUIRE(session.document.getFrameCount() == 65544);
    REQUIRE(session.cursor == 65530);
    REQUIRE_FALSE(session.selection.isActive());
    REQUIRE_FALSE(session.undoStore.isAttached());
    REQUIRE_THROWS_AS(session.document.getSample(0, 0), std::logic_error);
    REQUIRE_THROWS_AS(session.document.acquireReadLease().snapshotAudioBuffer(),
                      std::logic_error);
    auto cut = session.getEditRevision();
    f.state.undo();
    REQUIRE(session.getEditRevision() == f.original);
    REQUIRE(session.document.getMarkers() == markers);
    REQUIRE(session.cursor == 65560);
    REQUIRE(session.selection.getLengthInt() == 29);
    REQUIRE(f.state.clipboard.getFrameCount() ==
            29); // Clipboard survives undo.
    f.state.redo();
    REQUIRE(session.getEditRevision() == cut);
    f.state.undo();
    f.select(5, 3);
    actions::audio::performPaste(&f.state);
    auto pasted = session.getEditRevision();
    f.state.clipboard.clear();
    f.state.undo();
    f.state.redo();
    REQUIRE(session.getEditRevision() ==
            pasted); // Redo pins its original clipboard.
    REQUIRE(f.store->ioBytes() == io);
    std::vector<float> expected = f.samples;
    expected.erase(expected.begin() + 10, expected.begin() + 16);
    expected.insert(expected.begin() + 10, f.samples.begin() + 65530 * 2,
                    f.samples.begin() + 65559 * 2);
    REQUIRE(f.read() == expected);
}

TEST_CASE("Random production splice commands agree with a flat sample model",
          "[revision-commands]")
{
    Fixture f(521);
    auto model = f.samples;
    std::vector<std::vector<float>> versions{model};
    std::mt19937 random(1979);
    for (int step = 0; step < 80; ++step)
    {
        const auto frames = int64_t(model.size() / 2);
        const int64_t start = random() % (frames - 1);
        const int64_t count =
            std::min<int64_t>(1 + random() % 11, frames - start - 1);
        f.select(start, count);
        switch (step % 4)
        {
            case 0:
                actions::audio::performDelete(&f.state);
                model.erase(model.begin() + start * 2,
                            model.begin() + (start + count) * 2);
                break;
            case 1:
                actions::audio::performInsertSilence(&f.state, 19);
                model.erase(model.begin() + start * 2,
                            model.begin() + (start + count) * 2);
                model.insert(model.begin() + start * 2, 38, 0.f);
                break;
            case 2:
                f.state.clipboard.assignRevision(f.original);
                actions::audio::performPaste(&f.state);
                model.erase(model.begin() + start * 2,
                            model.begin() + (start + count) * 2);
                model.insert(model.begin() + start * 2, f.samples.begin(),
                             f.samples.end());
                break;
            case 3:
                f.state.getActiveViewState().selectedChannels =
                    SelectedChannels::LEFT;
                effects::performMakeSilent(&f.state);
                for (auto i = start; i < start + count; ++i)
                {
                    model[i * 2] = 0;
                }
                break;
        }
        REQUIRE(f.read() == model);
        versions.push_back(model);
    }
    for (int step = 79; step >= 0; --step)
    {
        f.state.undo();
        REQUIRE(f.read() == versions[step]);
    }
    for (int step = 1; step <= 80; ++step)
    {
        f.state.redo();
        REQUIRE(f.read() == versions[step]);
    }
    f.select(17, 201);
    const auto before = f.state.getActiveDocumentSession().getEditRevision();
    actions::audio::performTrim(&f.state);
    REQUIRE(f.read() ==
            std::vector<float>(model.begin() + 34, model.begin() + 436));
    f.state.undo();
    REQUIRE(f.state.getActiveDocumentSession().getEditRevision() == before);
}

TEST_CASE("Rejected revision commits retain redo and never change clipboard",
          "[revision-commands]")
{
    Fixture f(100);
    f.select(3, 4);
    actions::audio::performCut(&f.state);
    f.state.undo();
    auto &session = f.state.getActiveDocumentSession();
    auto old = actions::audio::RevisionEditState::capture(session);
    auto next = old;
    storage::AudioEditTransaction edit(*old.audio);
    edit.erase(0, 1);
    next.audio = edit.finish();
    old.audio = next.audio; // A stale expected root.
    auto rejected = std::make_shared<actions::audio::RevisionEdit>(
        &f.state, f.state.activeTabIndex, "Stale edit", old, next);
    const auto clip = f.state.clipboard.getRevision();
    f.state.addAndDoUndoable(rejected);
    REQUIRE_FALSE(rejected->lastOperationCommitted());
    REQUIRE(f.state.getActiveRedoables().size() == 1);
    REQUIRE(f.state.getActiveUndoables().empty());
    REQUIRE(f.state.clipboard.getRevision() == clip);
    REQUIRE(session.getEditRevision() == f.original);
}

TEST_CASE("Reference clipboard retains owned sources after document closure",
          "[revision-commands]")
{
    std::shared_ptr<const storage::AudioEditRevision> clip;
    std::filesystem::path path;
    {
        Fixture f(100);
        f.select(17, 13);
        actions::audio::performCopy(&f.state);
        clip = f.state.clipboard.getAudioRevision();
        path = f.store->path();
    }
    REQUIRE(std::filesystem::exists(path));
    std::array<float, 13> read{};
    clip->readChannel(0, 0, read);
    clip.reset();
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::filesystem::exists(path) &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::yield();
    }
    REQUIRE_FALSE(std::filesystem::exists(path));
}

TEST_CASE(
    "Streamed revision effects match resident effects across block boundaries",
    "[revision-effects]")
{
    using namespace actions::effects;
    auto kind = GENERATE(
        BackgroundEffectKind::Reverse, BackgroundEffectKind::AmplifyFade,
        BackgroundEffectKind::AmplifyEnvelope, BackgroundEffectKind::Dynamics,
        BackgroundEffectKind::RemoveSilence);
    const bool both = GENERATE(false, true);
    Fixture f(131123);
    Document reference;
    reference.initialize(SampleFormat::FLOAT32, 48000, 2, f.samples.size() / 2);
    reference.writeInterleavedFloatBlock(0, f.samples.data(),
                                         f.samples.size() / 2, 2, false);
    BackgroundEffectRequest request;
    request.kind = kind;
    request.startFrame = 13;
    request.frameCount = 131097;
    request.targetChannels =
        both ? std::vector<int64_t>{0, 1} : std::vector<int64_t>{1};
    request.amplifyFadeSettings =
        effects::AmplifyFadeSettings{23, 129, 2, false};
    request.amplifyEnvelopeSettings =
        effects::AmplifyEnvelopeSettings{{{0, 20}, {.37, 160}, {1, 51}}};
    request.dynamicsSettings = effects::DynamicsSettings{31, 3};
    request.removeSilenceSettings =
        effects::RemoveSilenceSettings{0, 0, -80, 0, .1};
    BackgroundEffectJob legacy(1, request, reference);
    legacy.start();
    REQUIRE(legacy.waitForCompletion(std::chrono::seconds(5)));
    REQUIRE(legacy.snapshot().success);
    auto expected = legacy.takeResult();
    const auto path = test::makeUniqueTestRoot("revision-effect") / "generated";
    BackgroundEffectJob disk(2, request,
                             f.state.getActiveDocumentSession().document, {},
                             nullptr, f.original, path);
    disk.start();
    REQUIRE(disk.waitForCompletion(std::chrono::seconds(5)));
    REQUIRE(disk.snapshot().success);
    auto actual = disk.takeResult();
    REQUIRE(actual->beforeRevision == f.original);
    REQUIRE(actual->oldSamples.empty());
    REQUIRE(actual->newSamples.empty());
    REQUIRE_FALSE(actual->preparedDocument);
    REQUIRE(expected->preparedDocument);
    auto lease = expected->preparedDocument->acquireReadLease();
    REQUIRE(actual->afterRevision->shape().frames == lease.getFrameCount());
    std::vector<float> a(lease.getFrameCount()), b(a.size());
    for (int c = 0; c < 2; ++c)
    {
        actual->afterRevision->readChannel(c, 0, a);
        lease.readChannelFloatBlock(c, 0, b.data(), b.size());
        REQUIRE(a == b);
    }
    // Unmodified source ranges retain the original block/source identity.
    actual->afterRevision->visitSourceRanges(
        0, 0, 13,
        [&](const auto &range)
        {
            REQUIRE(range.source);
            REQUIRE(range.source->blockStore() == f.store);
        });
    if (kind != BackgroundEffectKind::RemoveSilence)
    {
        // Summaries are built during processing, not by rescanning output.
        storage::AudioEditRevision::PeakWork work;
        REQUIRE(actual->afterRevision->prepareWaveform(work));
    }
}

TEST_CASE("Canceled revision effect cleans partial storage without committing",
          "[revision-effects]")
{
    Fixture f(131123);
    actions::effects::BackgroundEffectRequest request;
    request.kind = actions::effects::BackgroundEffectKind::Reverse;
    request.startFrame = 0;
    request.frameCount = 131123;
    request.targetChannels = {0};
    const auto path =
        test::makeUniqueTestRoot("cancel-revision-effect") / "generated";
    REQUIRE_THROWS_AS(
        actions::effects::computeRevisionEffect(
            request, f.original, path,
            [](const std::string &, std::optional<double> progress)
            {
                if (progress && *progress > .5)
                {
                    throw LongTaskCanceledError{};
                }
            }),
        LongTaskCanceledError);
    REQUIRE_FALSE(std::filesystem::exists(path));
    REQUIRE(f.state.getActiveDocumentSession().getEditRevision() == f.original);
}

TEST_CASE("Production effect publication commits roots and rejects stale work",
          "[revision-effects]")
{
    const bool stale = GENERATE(false, true);
    Fixture f(65573);
    auto &session = f.state.getActiveDocumentSession();
    f.select(65520, 37);
    const auto before = session.getEditRevision();
    std::string error;
    f.state.errorReporter = [&](const std::string &, const std::string &detail)
    {
        error = detail;
    };
    REQUIRE(actions::effects::queueReverse(&f.state));
    if (stale)
    {
        storage::AudioEditTransaction change(*before);
        change.erase(0, 1);
        REQUIRE(session.commitEditRevision(before, change.finish(), {}));
    }
    REQUIRE(f.state.backgroundEffectJob->waitForCompletion(
        std::chrono::seconds(5)));
    actions::effects::processPendingEffectWork(&f.state);
    if (stale)
    {
        REQUIRE_FALSE(error.empty());
        REQUIRE(f.state.getActiveUndoables().empty());
        REQUIRE(session.document.getFrameCount() == 65572);
    }
    else
    {
        REQUIRE(error.empty());
        REQUIRE(f.state.getActiveUndoables().size() == 1);
        auto expected = f.samples;
        for (int64_t i = 0; i < 37; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                expected[(65520 + i) * 2 + c] = f.samples[(65556 - i) * 2 + c];
            }
        }
        REQUIRE(f.read() == expected);
        auto after = session.getEditRevision();
        f.state.undo();
        REQUIRE(session.getEditRevision() == before);
        f.state.redo();
        REQUIRE(session.getEditRevision() == after);
    }
}
