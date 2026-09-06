#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "TestPaths.hpp"
#include "actions/io/BackgroundOpen.hpp"
#include "actions/io/BackgroundSave.hpp"
#include "actions/Save.hpp"
#include "actions/audio/SetSampleValue.hpp"
#include "file/OwnedSourceFile.hpp"
#include "file/OwnedAudioImport.hpp"
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
    // The production open wrote a cache under the original source key.
    int cachedPreviews = 0, generatedPreviews = 0;
    auto cached = file::importOwnedAudio(
        path, files.root / "cached",
        std::make_shared<storage::DecodedBlockCache>(0), {}, {},
        [&](const auto &chunk)
        {
            if (chunk.cached)
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
    REQUIRE(cachedPreviews == 1);
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
