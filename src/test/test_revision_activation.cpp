#include "TestRevisionCommands.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "TestPaths.hpp"
#include "actions/io/BackgroundOpen.hpp"
#include "actions/io/BackgroundSave.hpp"
#include "actions/Save.hpp"
#include "actions/markers/Split.hpp"
#include "actions/MutationAvailability.hpp"
#include "actions/DocumentTabs.hpp"
#include "actions/effects/BackgroundEffect.hpp"
#include "playback/PlaybackRange.hpp"
#include <latch>
#include "actions/audio/SetSampleValue.hpp"
#include "file/OwnedSourceFile.hpp"
#include "file/OwnedAudioImport.hpp"
#include "file/DecodedImportCache.hpp"
#include "persistence/DocumentAutosave.hpp"
#include <sndfile.h>
#include <thread>

using namespace cupuacu;
namespace
{
    struct Files
    {
        std::filesystem::path root =
            test::makeUniqueTestRoot("revision-activation");
        Files()
        {
            std::filesystem::create_directories(root);
        }
        ~Files()
        {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }
    };
    template <class F> void until(F done)
    {
        auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!done())
        {
            REQUIRE(std::chrono::steady_clock::now() < deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    void fixture(const std::filesystem::path &path)
    {
        SF_INFO info{};
        info.channels = 2;
        info.samplerate = 48000;
        info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_32;
        auto *file = sf_open(path.string().c_str(), SFM_WRITE, &info);
        REQUIRE(file);
        std::array<int, 65536> data;
        data.fill(0x40000001);
        for (int i = 0; i < 8; ++i)
        {
            REQUIRE(sf_writef_int(file, data.data(), data.size() / 2) ==
                    data.size() / 2);
        }
        REQUIRE(sf_close(file) == 0);
    }
    void open(State &state, const std::filesystem::path &path)
    {
        actions::io::queueOpenFile(&state, path.string());
        until(
            [&]
            {
                actions::io::processPendingOpenWork(&state);
                return !state.backgroundOpenJob &&
                       state.pendingOpenFiles.empty();
            });
        REQUIRE(state.getActiveDocumentSession().hasReadRevision());
    }
} // namespace
TEST_CASE("Normal opening commits owned audio and reusable source peaks",
          "[revision-activation]")
{
    Files files;
    auto path = files.root / "source.wav";
    fixture(path);
    test::StateWithTestPaths state{files.root / "state"};
    std::string error;
    state.errorReporter = [&](auto, auto text)
    {
        error = text;
    };
    open(state, path);
    auto &session = state.getActiveDocumentSession();
    REQUIRE(error.empty());
    REQUIRE_FALSE(session.openingPreview);
    REQUIRE_FALSE(state.longTask.active);
    REQUIRE_THROWS_AS(session.document.getAudioBuffer(), std::logic_error);
    REQUIRE(session.preservationSource);
    REQUIRE(session.preservationSource->blockStore()->ioBytes().first == 0);
    REQUIRE_FALSE(std::filesystem::equivalent(
        path, session.preservationSource->sourcePath()));
    session.retryImportedPeakPersistence();
    waveform::flushScheduledPersistentWaveformCaches();
    // The production open wrote a cache under the original source key.
    int cachedPreviews = 0, generatedPreviews = 0;
    auto cached = file::importOwnedAudio(
        path, files.root / "cached",
        std::make_shared<storage::DecodedBlockCache>(0), {}, {},
        [&](const auto &chunk)
        {
            if (chunk.cached || chunk.sourcePeaks)
            {
                ++cachedPreviews;
            }
            else if (chunk.toBlock >= chunk.fromBlock)
            {
                ++generatedPreviews;
            }
        },
        {.preferFilesystemClone = true,
         .waveformCacheRoot = state.paths->waveformCachePath(),
         .publishMetadata = true});
    REQUIRE(cached.metadata.persistentWaveformCacheLoaded);
    REQUIRE(cachedPreviews >= 1);
    REQUIRE(generatedPreviews == 0);
    std::filesystem::remove(path);
    std::array<float, 17> samples;
    session.getAudioReader()->readChannel(1, 65529, samples);
    for (auto sample : samples)
    {
        REQUIRE(sample == .5f);
    }
    storage::AudioEditRevision::PeakWork work;
    REQUIRE(session.getEditRevision()->prepareWaveform(work));
    REQUIRE(
        session.getEditRevision()->queryWaveformOverview(0, 0, 262144, work));
    auto original = session.getEditRevision();
    state.paths.reset();
    state.addAndDoUndoable(std::make_shared<actions::audio::SetSampleValue>(
        &state, 0, 7, .5f, -.25f));
    state.undo();
    REQUIRE(session.getEditRevision() == original);
}
TEST_CASE(
    "Format-changing Save As retains the new container across overwrite and "
    "recovery",
    "[revision-activation]")
{
    const bool background = GENERATE(false, true);
    Files files;
    auto source = files.root / "source.wav",
         output = files.root / "converted.aiff";
    fixture(source);
    test::StateWithTestPaths state{files.root / "state"};
    open(state, source);
    state.paths.reset();
    auto &session = state.getActiveDocumentSession();
    auto root = session.getEditRevision();
    auto originalContainer = session.preservationSource;
    auto settings =
        *file::defaultExportSettingsForPath(output, SampleFormat::PCM_S16);
    std::string error;
    state.errorReporter = [&](auto, auto text)
    {
        error = text;
    };
    if (background)
    {
        REQUIRE(actions::io::queueSaveAs(&state, output.string(), settings));
        until(
            [&]
            {
                actions::io::processPendingSaveWork(&state);
                return !state.backgroundSaveJob;
            });
    }
    else
    {
        REQUIRE(actions::saveAs(&state, output.string(), settings));
    }
    REQUIRE(error.empty());
    REQUIRE(session.getEditRevision() == root);
    REQUIRE(session.preservationSource != originalContainer);
    REQUIRE(session.preservationSource->shape().frames == 0);
    REQUIRE(session.preservationSource->blockStore()->ioBytes().second == 0);
    INFO(file::assessRevisionPreservation(session, settings).reason);
    REQUIRE(file::assessRevisionPreservation(session, settings).available);
    REQUIRE_FALSE(actions::documentSessionHasUnsavedChanges(session));
    std::filesystem::remove(output); // Next overwrite must use retained bytes.
    REQUIRE(actions::io::queueOverwritePreserving(&state));
    until(
        [&]
        {
            actions::io::processPendingSaveWork(&state);
            return !state.backgroundSaveJob;
        });
    REQUIRE(error.empty());
    SF_INFO info{};
    auto *saved = sf_open(output.string().c_str(), SFM_READ, &info);
    REQUIRE(saved);
    REQUIRE((info.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_AIFF);
    REQUIRE((info.format & SF_FORMAT_SUBMASK) == SF_FORMAT_PCM_16);
    REQUIRE(info.frames == 262144);
    sf_close(saved);
    auto checkpoint = files.root / "checkpoint";
    REQUIRE(persistence::saveDocumentAutosaveSnapshot(checkpoint, session));
    DocumentSession restored;
    REQUIRE(persistence::loadDocumentAutosaveSnapshot(checkpoint, restored));
    REQUIRE(file::assessRevisionPreservation(restored, settings).available);
}
TEST_CASE("Failure retaining a saved container leaves existing output intact",
          "[revision-activation]")
{
    Files files;
    auto output = files.root / "output.wav";
    std::ofstream(output) << "old";
    REQUIRE_THROWS(file::writeOwnedRevisionContainer(
        output, files.root / "working",
        storage::AudioShape{10, 1, 48000, SampleFormat::FLOAT32},
        [](const auto &owned)
        {
            std::ofstream(owned) << "new";
        },
        [](double)
        {
            throw LongTaskCanceledError{};
        }));
    std::ifstream in(output);
    std::string contents;
    in >> contents;
    REQUIRE(contents == "old");
}

TEST_CASE(
    "Queued effects leave other tabs editable and closed targets cannot "
    "publish",
    "[document-operations]")
{
    Files files;
    auto path = files.root / "source.wav";
    fixture(path);
    test::StateWithTestPaths state{files.root / "state"};
    open(state, path);
    open(state, path);
    REQUIRE(state.tabs.size() == 2);
    const auto secondRoot = state.tabs[1].session.getEditRevision();
    // Hold both bulk slots, keeping cancellation and publication deterministic.
    std::promise<void> release;
    auto gate = release.get_future().share();
    struct Release
    {
        std::promise<void> &p;
        ~Release()
        {
            p.set_value();
        }
    };
    std::latch started(2);
    auto block = [&]
    {
        started.count_down();
        gate.wait();
    };
    auto first = state.taskScheduler->submit(block, {});
    auto second = state.taskScheduler->submit(block, {});
    started.wait();
    {
        Release onExit{release};
        REQUIRE(actions::effects::queueReverse(&state));
        CHECK_FALSE(state.longTask.active);
        CHECK_FALSE(actions::isDocumentMutationAvailable(&state));
        CHECK_FALSE(actions::effects::queueReverse(&state));
        REQUIRE(actions::switchToTab(&state, 0));
        CHECK(actions::isDocumentMutationAvailable(&state));
        state.paths.reset();
        state.addAndDoUndoable(std::make_shared<actions::audio::SetSampleValue>(
            &state, 0, 7, .5f, -.25f));
        REQUIRE(actions::effects::queueReverse(&state));
        CHECK(state.additionalEffectJobs.size() == 1);
        REQUIRE(actions::closeTabWithoutConfirmation(&state, 1));
        actions::effects::processPendingEffectWork(&state);
    }
    until(
        [&]
        {
            actions::effects::processPendingEffectWork(&state);
            return !state.backgroundEffectJob;
        });
    REQUIRE(state.tabs.size() == 1);
    auto &session = state.getActiveDocumentSession();
    CHECK_FALSE(state.getActiveTab()->operation);
    std::array<float, 1> value;
    session.getAudioReader()->readChannel(
        0, session.document.getFrameCount() - 8, value);
    CHECK(value[0] == -.25f);
    secondRoot->readChannel(0, 7, value);
    CHECK(value[0] == .5f);
}

TEST_CASE("Canceling a queued import preserves unrelated tab edits",
          "[document-operations]")
{
    Files files;
    auto path = files.root / "source.wav";
    fixture(path);
    test::StateWithTestPaths state{files.root / "state"};
    open(state, path);
    std::promise<void> release;
    auto gate = release.get_future().share();
    struct Release
    {
        std::promise<void> &p;
        ~Release()
        {
            p.set_value();
        }
    };
    std::latch started(2);
    auto block = [&]
    {
        started.count_down();
        gate.wait();
    };
    auto first = state.taskScheduler->submit(block, {});
    auto second = state.taskScheduler->submit(block, {});
    started.wait();
    {
        Release onExit{release};
        actions::io::queueOpenFile(&state, path.string());
        actions::io::processPendingOpenWork(&state);
        REQUIRE(state.tabs.size() == 2);
        CHECK(state.getActiveDocumentSession().openingPreview);
        CHECK_FALSE(state.longTask.active);
        requestLongTaskCancel(&state);
        REQUIRE(actions::switchToTab(&state, 0));
        state.paths.reset();
        state.addAndDoUndoable(std::make_shared<actions::audio::SetSampleValue>(
            &state, 0, 7, .5f, -.25f));
        actions::io::processPendingOpenWork(&state);
        if (GENERATE(false, true))
        {
            REQUIRE(actions::switchToTab(&state, 1));
        }
    }
    until(
        [&]
        {
            actions::io::processPendingOpenWork(&state);
            return !state.backgroundOpenJob;
        });
    REQUIRE(state.tabs.size() == 1);
    CHECK(state.activeTabIndex == 0);
    std::array<float, 1> value;
    state.getActiveDocumentSession().getAudioReader()->readChannel(0, 7, value);
    CHECK(value[0] == -.25f);
}

TEST_CASE(
    "Imported sealed blocks support playback snapshots and pending raw views",
    "[document-operations]")
{
    Files files;
    auto path = files.root / "source.wav";
    fixture(path);
    DocumentSession preview;
    std::shared_ptr<const storage::AudioReader> pinned;
    int published = 0;
    auto imported = file::importOwnedAudio(
        path, files.root / "owned",
        std::make_shared<storage::DecodedBlockCache>(0), {}, {},
        [&](const auto &chunk)
        {
            if (!chunk.audio || !chunk.audio->availableFrames())
            {
                return;
            }
            preview.openingPreview = true;
            preview.openingAudio = chunk.audio;
            preview.document.setExternalAudioShape(
                chunk.format, chunk.sampleRate, int(chunk.channels.size()),
                chunk.frameCount);
            const auto available = chunk.audio->availableFrames();
            ++published;
            if (!pinned)
            {
                pinned = preview.getAudioReader();
            }
            std::array<float, 7> values;
            chunk.audio->readChannel(0, available - 7, values);
            CHECK(values[0] == .5f);
            CHECK(playback::computeRangeForPlay(preview, false).end ==
                  available);
            CHECK(playback::computeRangeForLiveUpdate(preview, true, 0,
                                                      pinned->shape().frames,
                                                      pinned->shape().frames)
                      .end == pinned->shape().frames);
            if (available < chunk.frameCount)
            {
                auto source = preview.getViewportSource();
                REQUIRE(source);
                auto data = waveform::WaveformViewport::compute(
                    *source, {0, available, 1, 20},
                    []
                    {
                        return false;
                    });
                REQUIRE(data);
                CHECK(data->pending);
                CHECK_THROWS(chunk.audio->readChannel(0, available, values));
            }
        },
        {.publishMetadata = true, .publishAudio = true});
    REQUIRE(pinned);
    CHECK(published >= 4);
    CHECK(pinned->shape().frames == storage::AudioBlockFrames);
    std::array<float, 1> value;
    pinned->readChannel(1, 3, value);
    CHECK(value[0] == .5f);
}

TEST_CASE(
    "Revision marker splitting shares source audio and preserves tab metadata",
    "[large-workflow]")
{
    Files files;
    auto path = files.root / "source.wav";
    fixture(path);
    test::StateWithTestPaths state{files.root / "state"};
    open(state, path);
    state.paths.reset();
    auto &source = state.getActiveDocumentSession();
    source.document.addMarker(13, "first");
    source.document.addMarker(65539, "second");
    source.document.addMarker(131079, "last");
    auto store = source.preservationSource->blockStore();
    const auto io = store->ioBytes();
    REQUIRE(actions::markers::splitByMarkers(&state));
    cupuacu::test::finishRevisionCommands(&state);
    REQUIRE(state.tabs.size() == 3);
    CHECK(state.activeTabIndex == 0);
    CHECK(store->ioBytes() == io);
    for (int i = 1; i < 3; ++i)
    {
        const auto &session = state.tabs[i].session;
        REQUIRE(session.hasReadRevision());
        CHECK(session.currentFile.empty());
        CHECK(session.revisionHasUnsavedChanges());
        const auto markers = session.document.getMarkers();
        REQUIRE(markers.size() == 2);
        CHECK(markers.front().frame == 0);
        CHECK(markers.back().frame == session.document.getFrameCount());
        CHECK(markers.front().label == (i == 1 ? "first" : "second"));
        std::array<float, 17> samples;
        session.getAudioReader()->readChannel(1, 3, samples);
        CHECK(samples.front() == .5f);
    }
}

TEST_CASE("Peak writer saturation and failure cannot delay imported edits",
          "[large-workflow]")
{
    Files files;
    auto path = files.root / "source.wav", other = files.root / "other.wav";
    fixture(path);
    fixture(other);
    test::StateWithTestPaths state{files.root / "state"};
    state.importSampleCache =
        std::make_shared<storage::DecodedBlockCache>(storage::AudioBlockBytes);
    open(state, path);
    auto snapshot = std::make_shared<waveform::PersistentCacheSnapshot>(
        *state.getActiveDocumentSession().pendingImportedPeaks);
    std::promise<void> release;
    auto gate = release.get_future().share();
    std::latch started(1);
    snapshot->beforeWrite = [&]
    {
        started.count_down();
        gate.wait();
        throw std::runtime_error("Injected peak write failure");
    };
    struct Release
    {
        std::promise<void> &p;
        ~Release()
        {
            p.set_value();
        }
    };
    {
        Release onExit{release};
        REQUIRE(waveform::schedulePersistentWaveformCache(snapshot) ==
                waveform::CacheSaveScheduleResult::Scheduled);
        started.wait();
        auto queued =
            std::make_shared<waveform::PersistentCacheSnapshot>(*snapshot);
        queued->beforeWrite = []
        {
            throw std::runtime_error("Injected queued write failure");
        };
        for (int i = 0; i < 4; ++i)
        {
            REQUIRE(waveform::schedulePersistentWaveformCache(queued) ==
                    waveform::CacheSaveScheduleResult::Scheduled);
        }
        open(state, other);
        REQUIRE(state.getActiveDocumentSession().pendingImportedPeaks);
        CHECK(actions::isDocumentMutationAvailable(&state));
        state.paths.reset();
        state.addAndDoUndoable(std::make_shared<actions::audio::SetSampleValue>(
            &state, 0, 7, .5f, -.25f));
        state.getActiveDocumentSession().retryImportedPeakPersistence();
        CHECK(state.getActiveDocumentSession().pendingImportedPeaks);
    }
    waveform::flushScheduledPersistentWaveformCaches();
    for (auto &tab : state.tabs)
    {
        tab.session.retryImportedPeakPersistence();
    }
    waveform::flushScheduledPersistentWaveformCaches();
    CHECK_FALSE(state.getActiveDocumentSession().pendingImportedPeaks);
    std::array<float, 1> sample;
    state.getActiveDocumentSession().getAudioReader()->readChannel(0, 7,
                                                                   sample);
    CHECK(sample[0] == -.25f);
    CHECK(state.importSampleCache->stats().peakResidentBytes <=
          storage::AudioBlockBytes);
    DocumentSession cacheSession;
    cacheSession.document = state.getActiveDocumentSession().document;
    cacheSession.currentFile = other.string();
    cacheSession.waveformCaches.resetToChannelCount(2);
    REQUIRE(
        waveform::loadPersistentWaveformCache(cacheSession, snapshot->root));
    CHECK(cacheSession.getWaveformCache(0)
              .snapshotBuildState()
              .levels[0][0]
              .min == .5f);
}

TEST_CASE("A stalled importer leaves sealed audio and async browsing available",
          "[large-workflow]")
{
    Files files;
    const auto path = files.root / "source.wav";
    fixture(path);
    std::promise<waveform::DecodedWaveformChunk> publication;
    auto published = publication.get_future();
    std::promise<void> release;
    auto gate = release.get_future().share();
    auto import = std::async(std::launch::async, [&]
    {
        bool sent = false;
        return file::importOwnedAudio(path, files.root / "owned",
            std::make_shared<storage::DecodedBlockCache>(storage::AudioBlockBytes), {}, {},
            [&](auto chunk)
            {
                if (!sent && chunk.audio && chunk.audio->availableFrames())
                {
                    sent = true;
                    publication.set_value(std::move(chunk));
                    gate.wait(); // Simulates a decoder waiting for more input.
                }
            }, {.publishMetadata = true, .publishAudio = true});
    });
    struct Release
    {
        std::promise<void> &promise;
        ~Release() { promise.set_value(); }
    } unblock{release};
    REQUIRE(published.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    auto chunk = published.get();
    DocumentSession session;
    session.openingPreview = true;
    session.openingAudio = chunk.audio;
    session.document.setExternalAudioShape(chunk.format, chunk.sampleRate,
                                          int(chunk.channels.size()), chunk.frameCount);
    auto playable = session.getAudioReader();
    REQUIRE(playable->shape().frames == storage::AudioBlockFrames);
    CHECK(playback::computeRangeForPlay(session, false).end == playable->shape().frames);
    CHECK(import.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);
    waveform::WaveformViewport view(*session.getViewportSource());
    const auto generation = view.submit({0, 10, .5, 100});
    std::optional<waveform::WaveformViewport::Result> ready;
    until([&] { ready = view.takePublished(); return bool(ready); });
    REQUIRE(ready->generation == generation);
    REQUIRE_FALSE(ready->error);
    REQUIRE(ready->value);
    CHECK_FALSE(ready->value->pending);
    for (auto value : ready->value->samples) CHECK(value == .5f);
    auto playback = std::async(std::launch::async, [&]
    {
        std::array<float, 512> values;
        playable->readChannel(1, 0, values);
        return values;
    });
    REQUIRE(playback.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    for (auto value : playback.get()) CHECK(value == .5f);
    CHECK(import.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);
}
namespace
{
    void seedDecodedCache(const std::shared_ptr<file::DecodedImportCache> &cache,
                          const std::filesystem::path &path,
                          const std::shared_ptr<concurrency::TaskScheduler> &scheduler)
    {
        static unsigned sequence = 0;
        auto imported = file::importOwnedAudio(path, path.parent_path() /
            ("working-" + std::to_string(++sequence)),
            std::make_shared<storage::DecodedBlockCache>(0));
        auto &metadata = imported.metadata;
        metadata.document.addMarker(13, "cached marker");
        metadata.ownedSource = imported.audio;
        metadata.audioRevision = storage::AudioEditRevision::from(imported.audio);
        cache->retain(file::DecodedImportCache::sourceIdentity(path), metadata, scheduler);
        until([] { return !file::DecodedImportCache::hasPendingWork(); });
    }
}
TEST_CASE("Decoded cache persists audio, precision and shared cache admission",
          "[decoded-import-cache]")
{
    Files files;
    auto source = files.root / "source.wav";
    fixture(source);
    auto scheduler = std::make_shared<concurrency::TaskScheduler>();
    auto cache = std::make_shared<file::DecodedImportCache>(files.root / "cache");
    seedDecodedCache(cache, source, scheduler);
    auto weak = std::weak_ptr(cache);
    cache.reset();
    until([&] { return weak.expired(); });
    cache = std::make_shared<file::DecodedImportCache>(files.root / "cache");
    auto samples = std::make_shared<storage::DecodedBlockCache>(storage::AudioBlockBytes);
    auto restored = cache->load(file::DecodedImportCache::sourceIdentity(source), samples);
    REQUIRE(restored);
    CHECK(cache->diskHitCount() == 1);
    REQUIRE(restored->audioRevision->shape().frames == 262144);
    auto another = cache->load(file::DecodedImportCache::sourceIdentity(source), samples);
    REQUIRE(another);
    CHECK(another->document.getPreservationSourceId() != restored->document.getPreservationSourceId());
    std::array<audio::SampleProvenance, 1> provenance;
    std::array<uint8_t, 1> dirty;
    another->ownedSource->readLegacyMetadata(0, 0, provenance, dirty);
    CHECK(provenance[0].sourceId == another->document.getPreservationSourceId());
    CHECK(dirty[0] == 0);
    CHECK(restored->decodedAudioCacheLoaded);
    CHECK(restored->ownedSource->blockStore()->ioBytes().second == 0);
    std::array<float, 65536> block;
    for (int c = 0; c < 2; ++c)
        for (int64_t first = 0; first < restored->audioRevision->shape().frames; first += block.size())
        {
            restored->audioRevision->readChannel(c, first, block);
            for (auto value : block) REQUIRE(value == .5f);
        }
    CHECK(samples->stats().peakResidentBytes == storage::AudioBlockBytes);
    CHECK(restored->document.getMarkers().size() == 1);
    CHECK(restored->document.getMarkers()[0].label == "cached marker");
    CHECK(restored->document.getMarkers()[0].frame == 13);
    std::ifstream originalBytes(source, std::ios::binary), cachedBytes(restored->ownedSource->sourcePath(), std::ios::binary);
    std::array<char, 4096> a, b;
    while (originalBytes.read(a.data(), a.size()) || originalBytes.gcount())
    {
        const auto count = originalBytes.gcount();
        cachedBytes.read(b.data(), count);
        REQUIRE(cachedBytes.gcount() == count);
        REQUIRE(std::equal(a.begin(), a.begin() + count, b.begin()));
    }
    CHECK(restored->exportSettings);
    CHECK(restored->document.getSampleFormat() == SampleFormat::PCM_S32);
    CHECK(std::filesystem::file_size(restored->ownedSource->sourcePath()) == std::filesystem::file_size(source));
    std::filesystem::remove(source);
    restored->audioRevision->readChannel(1, 0, std::span(block).first(1));
    CHECK(block[0] == .5f);
}
TEST_CASE("Decoded cache evicts unused entries and retains live archive readers",
          "[decoded-import-cache]")
{
    Files files;
    auto first = files.root / "one.wav", second = files.root / "two.wav";
    fixture(first); fixture(second);
    auto scheduler = std::make_shared<concurrency::TaskScheduler>();
    constexpr uint64_t budget = 6 * 1024 * 1024;
    auto cache = std::make_shared<file::DecodedImportCache>(files.root / "cache", budget);
    seedDecodedCache(cache, first, scheduler);
    // Recreate the cache service so lookup necessarily uses the disk archive.
    auto weak = std::weak_ptr(cache); cache.reset();
    until([&] { return weak.expired(); });
    cache = std::make_shared<file::DecodedImportCache>(files.root / "cache", budget);
    auto pinned = cache->load(file::DecodedImportCache::sourceIdentity(first),
                             std::make_shared<storage::DecodedBlockCache>(0));
    REQUIRE(pinned);
    REQUIRE(cache->diskHitCount() == 1);
    seedDecodedCache(cache, second, scheduler);
    CHECK(cache->diskBytes() <= budget);
    REQUIRE(cache->load(file::DecodedImportCache::sourceIdentity(first),
                        std::make_shared<storage::DecodedBlockCache>(0)));
    std::array<float, 1> sample;
    pinned->audioRevision->readChannel(1, 3, sample);
    CHECK(sample[0] == .5f);
    auto competing = std::make_shared<file::DecodedImportCache>(files.root / "cache", 1);
    CHECK_FALSE(competing->load(file::DecodedImportCache::sourceIdentity(first),
                               std::make_shared<storage::DecodedBlockCache>(0)));
    pinned.reset();
    until([&] {
        for (const auto &entry : std::filesystem::directory_iterator(files.root / "cache"))
            if (entry.is_directory() && storage::RevisionArchive::hasLiveReaders(entry.path() / "manifest")) return false;
        return true;
    });
    seedDecodedCache(cache, second, scheduler);
    CHECK(cache->diskBytes() <= budget);
    CHECK_FALSE(cache->load(file::DecodedImportCache::sourceIdentity(first),
                            std::make_shared<storage::DecodedBlockCache>(0)));
    REQUIRE(cache->load(file::DecodedImportCache::sourceIdentity(second),
                        std::make_shared<storage::DecodedBlockCache>(0)));
}
TEST_CASE("Stale, damaged and unavailable decoded caches fall back cleanly",
          "[decoded-import-cache]")
{
    Files files;
    auto path = files.root / "source.wav";
    fixture(path);
    auto scheduler = std::make_shared<concurrency::TaskScheduler>();
    auto cache = std::make_shared<file::DecodedImportCache>(files.root / "cache");
    auto original = file::DecodedImportCache::sourceIdentity(path);
    seedDecodedCache(cache, path, scheduler);
    auto weak = std::weak_ptr(cache); cache.reset();
    until([&] { return weak.expired(); });
    cache = std::make_shared<file::DecodedImportCache>(files.root / "cache");
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(1));
    auto changed = file::DecodedImportCache::sourceIdentity(path);
    REQUIRE(changed != original);
    CHECK_FALSE(cache->load(changed, std::make_shared<storage::DecodedBlockCache>(0)));
    for (const auto &entry : std::filesystem::recursive_directory_iterator(files.root / "cache"))
        if (entry.path().filename() == "samples-0.bin") std::filesystem::resize_file(entry.path(), 1);
    CHECK_FALSE(cache->load(original, std::make_shared<storage::DecodedBlockCache>(0)));
    std::ofstream(files.root / "blocked") << "not a directory";
    auto blocked = std::make_shared<file::DecodedImportCache>(files.root / "blocked");
    CHECK_FALSE(blocked->load(changed, std::make_shared<storage::DecodedBlockCache>(0)));
    CHECK_NOTHROW(seedDecodedCache(blocked, path, scheduler));
}
TEST_CASE("Queued reopening reuses decoded audio and shows the original filename",
          "[decoded-import-cache]")
{
    Files files;
    auto path = files.root / "source.wav";
    fixture(path);
    test::StateWithTestPaths state{files.root / "state"};
    open(state, path);
    auto original = state.getActiveDocumentSession().preservationSource;
    const auto written = original->blockStore()->ioBytes().second;
    CHECK(written > 0);
    REQUIRE(actions::closeTabWithoutConfirmation(&state, 0));
    open(state, path);
    CHECK(state.decodedImportCache->hitCount() == 1);
    CHECK(state.getActiveDocumentSession().preservationSource->blockStore() == original->blockStore());
    CHECK(original->blockStore()->ioBytes().second == written);
    CHECK_FALSE(state.getActiveDocumentSession().revisionHasUnsavedChanges());
    until([] { return !file::DecodedImportCache::hasPendingWork(); });
    bool sawPreparing = false;
    file::importOwnedAudio(path, files.root / "progress", std::make_shared<storage::DecodedBlockCache>(0),
        [&](const std::string &detail, auto)
        {
            CHECK(detail.find("state/import-") == std::string::npos);
            CHECK(detail.find("source.wav") != std::string::npos);
            sawPreparing = sawPreparing || detail.starts_with("Preparing audio:");
        });
    CHECK(sawPreparing);
}

TEST_CASE("Pending decoded cache fills are bounded and allow live reuse",
          "[decoded-import-cache]")
{
    Files files;
    auto path = files.root / "source.wav";
    fixture(path);
    auto imported = file::importOwnedAudio(path, files.root / "working",
        std::make_shared<storage::DecodedBlockCache>(0));
    imported.metadata.ownedSource = imported.audio;
    imported.metadata.audioRevision = storage::AudioEditRevision::from(imported.audio);
    auto scheduler = std::make_shared<concurrency::TaskScheduler>();
    auto entered = std::make_shared<std::latch>(2);
    std::promise<void> release;
    auto gate = release.get_future().share();
    for (int i = 0; i < 2; ++i)
        (void)scheduler->submit([entered, gate] { entered->count_down(); gate.wait(); }, {});
    struct Release { std::promise<void> &p; ~Release() { p.set_value(); } } unblock{release};
    entered->wait();
    auto cache = std::make_shared<file::DecodedImportCache>(files.root / "cache");
    const auto key = file::DecodedImportCache::sourceIdentity(path);
    cache->retain(key, imported.metadata, scheduler);
    cache->retain(key, imported.metadata, scheduler);
    CHECK(scheduler->stats().queued == 1);
    auto ready = cache->load(key, std::make_shared<storage::DecodedBlockCache>(0));
    REQUIRE(ready);
    CHECK(cache->hitCount() == 1);
    CHECK(cache->diskHitCount() == 0);
    CHECK(ready->ownedSource->blockStore() == imported.audio->blockStore());
}
