#include "TestRevisionCommands.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "TestPaths.hpp"
#include "State.hpp"
#include "actions/audio/EditCommands.hpp"
#include "actions/effects/RevisionEffect.hpp"
#include "effects/MakeSilentEffect.hpp"
#include "waveform/DecodedWaveformBuilder.hpp"
#include "persistence/DocumentAutosave.hpp"
#include "persistence/RevisionPersistence.hpp"
#include <random>

using namespace cupuacu;
namespace
{
    struct Fixture
    {
        test::StateWithTestPaths state{std::string_view{"revision-editing"}};
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

TEST_CASE("Restored revision clipboard pastes into an empty tab by reference",
          "[revision-commands][clipboard-empty]")
{
    const bool configured = GENERATE(false, true);
    Fixture source;
    source.select(17, 65541);
    actions::audio::performCopy(&source.state);
    cupuacu::test::finishRevisionCommands(&source.state);
    const auto path = test::makeUniqueTestRoot("restored-paste") / "clipboard";
    REQUIRE(persistence::saveClipboardSnapshot(path, source.state.clipboard));
    test::StateWithTestPaths target{std::string_view{"empty-paste"}};
    REQUIRE(persistence::loadClipboardSnapshot(path, target.clipboard));
    auto &session = target.getActiveDocumentSession();
    if (configured)
    {
        session.document.initialize(SampleFormat::FLOAT32, 44100, 1, 0);
    }
    auto clip = target.clipboard.getAudioRevision();
    std::shared_ptr<storage::AudioBlockStore> store;
    clip->visitSourceRanges(0, 0, 1, [&](const auto &range)
                           { store = range.source->blockStore(); });
    REQUIRE(store);
    const auto io = store->ioBytes();
    actions::audio::performPaste(&target);
    cupuacu::test::finishRevisionCommands(&target);
    REQUIRE_FALSE(target.backgroundClipboardConversion);
    REQUIRE(session.hasReadRevision());
    REQUIRE(session.document.getFrameCount() == 65541);
    REQUIRE(session.document.getChannelCount() == (configured ? 1 : 2));
    REQUIRE(session.document.getSampleRate() == (configured ? 44100 : 48000));
    REQUIRE(session.revisionHasUnsavedChanges());
    REQUIRE(session.selection.getStartInt() == 0);
    REQUIRE(session.selection.getLengthInt() == 65541);
    REQUIRE_FALSE(session.undoStore.isAttached());
    REQUIRE(target.getActiveUndoables().size() == 1);
    const auto pasted = session.getEditRevision();
    target.clipboard.clear();
    target.undo();
    REQUIRE(session.document.getFrameCount() == 0);
    REQUIRE_FALSE(session.revisionHasUnsavedChanges());
    REQUIRE_FALSE(session.selection.isActive());
    target.redo();
    REQUIRE(session.getEditRevision() == pasted);
    REQUIRE(store->ioBytes() == io);
    std::vector<float> samples(65541);
    for (int c = 0; c < session.document.getChannelCount(); ++c)
    {
        session.getAudioReader()->readChannel(c, 0, samples);
        for (int i = 0; i < 65541; ++i)
        {
            REQUIRE(samples[i] == source.samples[(i + 17) * 2 + c]);
        }
    }
    const auto autosave = path.parent_path() / "pasted-document";
    persistence::RevisionPersistence::save(
        autosave, *persistence::RevisionPersistence::capture(
                      session, target.getActiveTab()));
    test::StateWithTestPaths recovered{std::string_view{"recovered-paste"}};
    auto &restored = recovered.getActiveDocumentSession();
    REQUIRE(persistence::loadDocumentAutosaveSnapshot(autosave, restored));
    REQUIRE(persistence::RevisionPersistence::installHistory(&recovered, 0));
    REQUIRE(restored.document.getFrameCount() == 65541);
    recovered.undo();
    REQUIRE(restored.document.getFrameCount() == 0);
    REQUIRE_FALSE(restored.revisionHasUnsavedChanges());
    recovered.redo();
    REQUIRE(restored.document.getFrameCount() == 65541);
}

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
    cupuacu::test::finishRevisionCommands(&f.state);
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
    cupuacu::test::finishRevisionCommands(&f.state);
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
                cupuacu::test::finishRevisionCommands(&f.state);
                model.erase(model.begin() + start * 2,
                            model.begin() + (start + count) * 2);
                break;
            case 1:
                actions::audio::performInsertSilence(&f.state, 19);
                cupuacu::test::finishRevisionCommands(&f.state);
                model.erase(model.begin() + start * 2,
                            model.begin() + (start + count) * 2);
                model.insert(model.begin() + start * 2, 38, 0.f);
                break;
            case 2:
                f.state.clipboard.assignRevision(f.original);
                actions::audio::performPaste(&f.state);
                cupuacu::test::finishRevisionCommands(&f.state);
                model.erase(model.begin() + start * 2,
                            model.begin() + (start + count) * 2);
                model.insert(model.begin() + start * 2, f.samples.begin(),
                             f.samples.end());
                break;
            case 3:
                f.state.getActiveViewState().selectedChannels =
                    SelectedChannels::LEFT;
                effects::performMakeSilent(&f.state);
                cupuacu::test::finishRevisionCommands(&f.state);
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
    cupuacu::test::finishRevisionCommands(&f.state);
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
    cupuacu::test::finishRevisionCommands(&f.state);
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
        cupuacu::test::finishRevisionCommands(&f.state);
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

#include "actions/audio/SetSampleValue.hpp"
#include "actions/audio/ClipboardPaste.hpp"
#include "storage/ClipboardConversion.hpp"
#include "effects/PeakAnalysis.hpp"

TEST_CASE(
    "A reference sample gesture retains one before/after root without I/O",
    "[revision-ui]")
{
    Fixture f;
    auto &session = f.state.getActiveDocumentSession();
    const auto before = session.getEditRevision();
    const auto io = f.store->ioBytes();
    auto edit = std::make_shared<actions::audio::SetSampleValue>(
        &f.state, 1, 65535, f.samples[65535 * 2 + 1]);
    for (int i = 0; i < 100; ++i)
    {
        edit->setNewValue(float(i) / 64);
        edit->redo();
        REQUIRE(edit->lastOperationCommitted());
    }
    f.state.addUndoable(edit);
    auto after = session.getEditRevision();
    REQUIRE(after->indexHeight() <= 3);
    REQUIRE(f.store->ioBytes() == io);
    REQUIRE_FALSE(edit->canPersistForRestart());
    f.state.undo();
    REQUIRE(session.getEditRevision() == before);
    f.state.redo();
    REQUIRE(session.getEditRevision() == after);
    REQUIRE(f.store->ioBytes() == io);
    std::array<float, 3> samples;
    after->readChannel(1, 65534, samples);
    REQUIRE(samples[0] == f.samples[65534 * 2 + 1]);
    REQUIRE(samples[1] == 99.f / 64);
    REQUIRE(samples[2] == f.samples[65536 * 2 + 1]);
    storage::AudioEditRevision::PeakWork work;
    REQUIRE(after->prepareWaveform(work));
    REQUIRE(after->queryWaveformOverview(1, 65535, 1, work)->max == 99.f / 64);
}

TEST_CASE(
    "Normalization excludes boundary spikes and uses exact revision summaries",
    "[revision-ui]")
{
    Fixture f;
    storage::AudioEditTransaction edit(*f.original);
    edit.replaceChannel(0, 17, 1, nullptr, 0, 0, 100);
    edit.replaceChannel(0, 41, 1, nullptr, 0, 0, 2);
    auto source = edit.finish();
    effects::PeakAnalysisRequest request{18, 65119, {0}};
    const auto before = f.store->ioBytes();
    auto peak =
        effects::PeakAnalysis::compute(*source, source.get(), nullptr, request,
                                       []
                                       {
                                           return false;
                                       });
    REQUIRE(peak == 2.f);
    REQUIRE(f.store->ioBytes().first - before.first <=
            2 * storage::AudioBlockBytes);
    request.start = 17;
    REQUIRE(effects::PeakAnalysis::compute(*source, source.get(), nullptr,
                                           request,
                                           []
                                           {
                                               return false;
                                           }) == 100.f);
    request.channels = {1};
    auto expected = 0.f;
    for (int64_t i = request.start; i < request.start + request.count; ++i)
    {
        expected = std::max(expected, std::fabs(f.samples[i * 2 + 1]));
    }
    REQUIRE(effects::PeakAnalysis::compute(*source, source.get(), nullptr,
                                           request,
                                           []
                                           {
                                               return false;
                                           }) == expected);
    REQUIRE_FALSE(effects::PeakAnalysis::compute(*source, source.get(), nullptr,
                                                 request,
                                                 []
                                                 {
                                                     return true;
                                                 }));
}

TEST_CASE("Clipboard conversion roundtrips samples and preservation metadata",
          "[revision-ui]")
{
    Document doc;
    doc.initialize(SampleFormat::PCM_S32, 44100, 2, 65573);
    for (int64_t i = 0; i < doc.getFrameCount(); ++i)
    {
        for (int c = 0; c < 2; ++c)
        {
            doc.setSample(c, i, float((i + c) % 17) / 32, false);
        }
    }
    doc.markCurrentStateAsSavedSource();
    doc.setSample(0, 65535, .875f);
    doc.setSampleProvenance(1, 7, {123, 300});
    ClipboardAudio original;
    original.assignSegment(doc.captureSegment(0, doc.getFrameCount()));
    const auto path =
        test::makeUniqueTestRoot("clipboard-conversion") / "audio";
    auto disk = storage::convertClipboard(original, true, path,
                                          []
                                          {
                                              return false;
                                          });
    REQUIRE(disk.getAudioRevision());
    auto restored = storage::convertClipboard(disk, false, {},
                                              []
                                              {
                                                  return false;
                                              });
    auto old = original.acquireReadLease();
    auto next = restored.acquireReadLease();
    for (int c = 0; c < 2; ++c)
    {
        for (int64_t i = 0; i < doc.getFrameCount(); ++i)
        {
            REQUIRE(next.getSample(c, i) == old.getSample(c, i));
            REQUIRE(next.isDirty(c, i) == old.isDirty(c, i));
            REQUIRE(next.getSampleProvenance(c, i).sourceId ==
                    old.getSampleProvenance(c, i).sourceId);
            REQUIRE(next.getSampleProvenance(c, i).frameIndex ==
                    old.getSampleProvenance(c, i).frameIndex);
        }
    }
    auto shape = disk.getAudioRevision()->shape();
    shape.channels = 3;
    shape.sampleRate = 48000;
    shape.format = SampleFormat::FLOAT32;
    auto mapped = disk.getAudioRevision()->forPaste(shape);
    REQUIRE(mapped->shape().sampleRate == 48000);
    std::array<float, 7> padded;
    mapped->readChannel(2, 65530, padded);
    REQUIRE(std::all_of(padded.begin(), padded.end(),
                        [](float value)
                        {
                            return value == 0;
                        }));
    storage::AudioEditRevision::PeakWork work;
    REQUIRE(mapped->prepareWaveform(work));
    auto canceledPath = test::makeUniqueTestRoot("cancel-clipboard") / "audio";
    int checks = 0;
    REQUIRE_THROWS_AS(storage::convertClipboard(original, true, canceledPath,
                                                [&]
                                                {
                                                    return ++checks > 3;
                                                }),
                      LongTaskCanceledError);
    REQUIRE_FALSE(std::filesystem::exists(canceledPath));
}

TEST_CASE(
    "Mixed backend paste pins accepted clipboard and publishes through history",
    "[revision-ui]")
{
    const bool toRevision = GENERATE(false, true);
    Fixture f(1000);
    auto &session = f.state.getActiveDocumentSession();
    ClipboardAudio incoming;
    if (toRevision)
    {
        incoming.initialize(SampleFormat::PCM_S16, 44100, 1, 5);
        for (int i = 0; i < 5; ++i)
        {
            incoming.setSample(0, i, float(i) / 8);
        }
    }
    else
    {
        storage::AudioEditTransaction slice(*f.original);
        slice.trim(40, 5);
        incoming.assignRevision(slice.finish());
        session.clearReadRevision();
        session.document.initialize(SampleFormat::FLOAT32, 48000, 2, 1000);
        session.document.writeInterleavedFloatBlock(0, f.samples.data(), 1000,
                                                    2, false);
        session.rebuildWaveformCacheSynchronously();
    }
    f.select(17, 3);
    f.state.clipboard = incoming;
    actions::audio::performPaste(&f.state);
    cupuacu::test::finishRevisionCommands(&f.state);
    REQUIRE(f.state.backgroundClipboardConversion);
    REQUIRE(f.state.getActiveUndoables().empty());
    f.state.clipboard.clear(); // Accepted paste retains its submitted contents.
    const auto clipboardVersion = f.state.clipboard.getRevision();
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (f.state.backgroundClipboardConversion &&
           std::chrono::steady_clock::now() < deadline)
    {
        actions::audio::processPendingClipboardPaste(&f.state);
        std::this_thread::yield();
    }
    REQUIRE_FALSE(f.state.backgroundClipboardConversion);
    cupuacu::test::finishRevisionCommands(&f.state);
    REQUIRE(f.state.getActiveUndoables().size() == 1);
    REQUIRE(session.document.getFrameCount() == 1002);
    REQUIRE(f.state.clipboard.getRevision() == clipboardVersion);
    const auto after = f.read();
    for (int i = 0; i < 5; ++i)
    {
        REQUIRE(after[(17 + i) * 2] ==
                (toRevision ? float(i) / 8 : f.samples[(40 + i) * 2]));
        REQUIRE(after[(17 + i) * 2 + 1] ==
                (toRevision ? 0.f : f.samples[(40 + i) * 2 + 1]));
    }
    f.state.undo();
    REQUIRE(f.read() == f.samples);
    f.state.redo();
    REQUIRE(f.read() == after);
}

#include "gui/SamplePoint.hpp"
#include "gui/Waveform.hpp"

TEST_CASE("Effect admission failure completes without changing its revision",
          "[working-memory]")
{
    Fixture f(1000);
    auto memory = std::make_shared<storage::DecodedBlockCache>(1024 * 1024);
    auto retained =
        memory->tryReserveWorking(800 * 1024, storage::MemoryUse::Peaks);
    auto scheduler =
        std::make_shared<cupuacu::concurrency::TaskScheduler>(1, 8, 1024 * 1024, memory);
    actions::effects::BackgroundEffectRequest request;
    request.kind = actions::effects::BackgroundEffectKind::Reverse;
    request.frameCount = 1000;
    request.targetChannels = {0, 1};
    auto &session = f.state.getActiveDocumentSession();
    actions::effects::BackgroundEffectJob job(
        1, request, session.document, {}, nullptr, f.original,
        test::makeUniqueTestRoot("admission-effect") / "working");
    job.start(scheduler);
    REQUIRE(job.waitForCompletion(std::chrono::seconds(2)));
    CHECK_FALSE(job.snapshot().success);
    CHECK_FALSE(job.snapshot().error.empty());
    CHECK(session.getEditRevision() == f.original);
    CHECK_FALSE(job.takeResult());
}

TEST_CASE(
    "A sample point consumes published values and survives refresh during "
    "dragging",
    "[revision-ui]")
{
    Fixture f(1000);
    gui::Waveform waveform(&f.state, 0);
    f.state.waveforms.push_back(&waveform);
    f.state.getActiveViewState().samplesPerPixel = .01;
    waveform.setBounds(0, 0, 640, 120);
    auto &session = f.state.getActiveDocumentSession();
    const auto before = session.getEditRevision();
    auto owned =
        std::make_unique<gui::SamplePoint>(&f.state, 0, 41, f.samples[82]);
    auto *point = owned.get();
    point->setBounds(10, 40, 12, 12);
    waveform.addChild(owned);
    const auto io = f.store->ioBytes();
    REQUIRE(point->mouseDown(
        {gui::DOWN, 0, 0, 0, 0, 0, 0, {true, false, false}, 1}));
    REQUIRE(point->mouseMove(
        {gui::MOVE, 0, 0, 0, 0, 0, -5, {true, false, false}, 1}));
    const auto value = point->getSampleValue();
    waveform.updateSamplePoints();
    REQUIRE(waveform.getChildren().size() == 1);
    REQUIRE(
        point->mouseUp({gui::UP, 0, 0, 0, 0, 0, 0, {false, false, false}, 1}));
    cupuacu::test::finishRevisionCommands(&f.state);
    REQUIRE(f.state.getActiveUndoables().size() == 1);
    REQUIRE(f.store->ioBytes() == io);
    f.state.undo();
    REQUIRE(session.getEditRevision() == before);
    f.state.redo();
    std::array<float, 1> sample;
    session.getAudioReader()->readChannel(0, 41, sample);
    REQUIRE(sample[0] == value);
    REQUIRE_FALSE(waveform.requestSampleValue(
        42)); // Coarse/missing viewport uses async I/O.
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);
    std::optional<float> hovered;
    while (!hovered && std::chrono::steady_clock::now() < deadline)
    {
        waveform.timerCallback();
        hovered = waveform.requestSampleValue(42);
        std::this_thread::yield();
    }
    REQUIRE(hovered);
    REQUIRE(*hovered == f.samples[84]);
    waveform.clearHighlight();
    f.state.waveforms.clear();
}

TEST_CASE("Accepted structural edits prepare away from the publishing thread",
          "[revision-commands]")
{
    Fixture f(1000);
    f.select(100, 200);
    const auto before = f.state.getActiveDocumentSession().getEditRevision();
    const auto accesses = storage::EditTree::threadAccesses;
    actions::audio::performCut(&f.state);
    REQUIRE(f.state.getActiveDocumentSession().getEditRevision() == before);
    REQUIRE(f.state.getActiveTab()->operation);
    cupuacu::test::finishRevisionCommands(&f.state);
    CHECK(storage::EditTree::threadAccesses == accesses);
    CHECK(f.state.getActiveDocumentSession().document.getFrameCount() == 800);
    CHECK(f.state.clipboard.getFrameCount() == 200);
    f.state.undo();
    CHECK(f.state.getActiveDocumentSession().getEditRevision() == before);
    CHECK(storage::EditTree::threadAccesses == accesses);
}

TEST_CASE("Canceled structural edits do not change audio history or clipboard",
          "[revision-commands]")
{
    Fixture f(1000);
    f.select(100, 200);
    actions::audio::performCut(&f.state);
    f.state.getActiveTab()->operation->cancelRequested = true;
    cupuacu::test::finishRevisionCommands(&f.state);
    CHECK(f.state.getActiveDocumentSession().getEditRevision() == f.original);
    CHECK(f.state.getActiveUndoables().empty());
    CHECK(f.state.clipboard.getFrameCount() == 0);
}

TEST_CASE("Clipboard preparation remains attached to its tab across navigation",
          "[revision-ui]")
{
    Fixture f(1000);
    auto &state = f.state;
    state.taskScheduler = std::make_shared<cupuacu::concurrency::TaskScheduler>(1, 8);
    ClipboardAudio clip;
    clip.initialize(SampleFormat::FLOAT32, 48000, 2, 5);
    for (int c = 0; c < 2; ++c)
    {
        for (int i = 0; i < 5; ++i)
        {
            clip.setSample(c, i, .5f);
        }
    }
    state.clipboard = std::move(clip);
    std::promise<void> release;
    auto gate = release.get_future().share();
    auto blocker = state.taskScheduler->submit(
        [gate]
        {
            gate.wait();
        },
        {});
    actions::audio::beginClipboardPaste(&state, 17, -1);
    const bool hasOperation = state.tabs[0].operation.has_value();
    const bool globalBlock = state.longTask.active;
    state.tabs.emplace_back();
    state.activeTabIndex = 1;
    release.set_value();
    REQUIRE(hasOperation);
    REQUIRE_FALSE(globalBlock);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (state.backgroundClipboardConversion &&
           std::chrono::steady_clock::now() < deadline)
    {
        actions::audio::processPendingClipboardPaste(&state);
        std::this_thread::yield();
    }
    REQUIRE_FALSE(state.backgroundClipboardConversion);
    test::finishRevisionCommands(&state);
    REQUIRE(state.tabs[0].session.document.getFrameCount() == 1005);
    REQUIRE(state.tabs[0].undoables.size() == 1);
    REQUIRE(state.tabs[1].session.document.getFrameCount() == 0);
    REQUIRE(state.activeTabIndex == 1);
}
