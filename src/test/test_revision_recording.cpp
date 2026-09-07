#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "TestPaths.hpp"
#include "storage/RecordingWriter.hpp"
#include "actions/audio/RevisionRecording.hpp"
#include "actions/MutationAvailability.hpp"
#include "actions/DocumentTabs.hpp"
#include "audio/AudioDevices.hpp"
#include "persistence/RevisionPersistence.hpp"
#include "persistence/DocumentAutosave.hpp"
#include "file/file_loading.hpp"
#include <chrono>
#include <thread>

using namespace cupuacu;
namespace
{
    struct Files
    {
        std::filesystem::path root =
            test::makeUniqueTestRoot("revision-record");
        ~Files()
        {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }
    };
    auto silent(int64_t frames, int channels = 2)
    {
        return storage::AudioEditRevision::silence(
            {frames, channels, 48000, SampleFormat::FLOAT32});
    }
    void bind(DocumentSession &session,
              std::shared_ptr<const storage::AudioEditRevision> audio)
    {
        const auto shape = audio->shape();
        session.document.initialize(shape.format, shape.sampleRate,
                                    shape.channels, shape.frames);
        session.bindReadRevision(std::move(audio));
    }
    audio::RecordedChunk chunk(int64_t start, uint32_t frames = 256,
                               int channels = 2)
    {
        audio::RecordedChunk result{start, frames, uint8_t(channels), {}};
        for (uint32_t i = 0; i < frames; ++i)
        {
            for (int c = 0; c < channels; ++c)
            {
                result.interleavedSamples[i * 2 + c] =
                    float((start + i + c) % 127) / 128;
            }
        }
        return result;
    }
    template <class F> void until(F done)
    {
        auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!done())
        {
            REQUIRE(std::chrono::steady_clock::now() < deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    float read(const storage::AudioReader &audio, int channel, int64_t frame)
    {
        float value;
        audio.readChannel(channel, frame, {&value, 1});
        return value;
    }
} // namespace
TEST_CASE(
    "Recording streams overwrite and extension with one immutable history "
    "entry",
    "[revision-recording]")
{
    Files files;
    State state;
    state.paths.reset();
    const int channels = GENERATE(1, 2);
    auto &session = state.getActiveDocumentSession();
    auto original = silent(10000, channels);
    bind(session, original);
    session.cursor = 123;
    session.selection.setValue1(123);
    session.selection.setValue2(400);
    actions::startRevisionRecording(&state, 123, files.root / "audio");
    REQUIRE_FALSE(actions::isDocumentMutationAvailable(&state));
    REQUIRE_FALSE(actions::canSwitchTabs(&state));
    auto recording = state.revisionRecording;
    for (int64_t at = 123; at < 123 + 16384; at += 256)
    {
        REQUIRE(recording->writer.submit(chunk(at, 256, channels)));
    }
    until(
        [&]
        {
            return recording->writer.snapshot().endFrame >= 123 + 16384;
        });
    REQUIRE(actions::pollRevisionRecording(&state));
    REQUIRE(session.document.getFrameCount() == 123 + 16384);
    REQUIRE(state.getActiveTab()->undoables.empty());
    REQUIRE(read(*original, 0, 123) == 0);
    REQUIRE(recording->writer.submit(chunk(123 + 16384, 37, channels)));
    recording->finishing = true;
    recording->writer.finish();
    until(
        [&]
        {
            actions::pollRevisionRecording(&state);
            return !state.revisionRecording;
        });
    REQUIRE(recording->writer.snapshot().error.empty());
    REQUIRE(recording->writer.snapshot().sampleBytesWritten ==
            (16384 + 37) * channels * sizeof(float));
    REQUIRE(state.getActiveTab()->undoables.size() == 1);
    auto recorded = session.getEditRevision();
    for (int c = 0; c < channels; ++c)
    {
        for (int64_t f = 0; f < recorded->shape().frames; ++f)
        {
            REQUIRE(read(*recorded, c, f) ==
                    (f < 123 ? 0.f : float((f + c) % 127) / 128));
        }
    }
    storage::AudioEditRevision::PeakWork work;
    REQUIRE(recorded->prepareWaveform(work));
    auto peak =
        recorded->queryWaveformOverview(0, 0, recorded->shape().frames, work);
    REQUIRE(peak);
    REQUIRE(peak->min == 0);
    REQUIRE(peak->max == 126.f / 128);
    auto undo = state.getActiveTab()->undoables.back();
    undo->undo();
    REQUIRE(session.getEditRevision() == original);
    REQUIRE(session.cursor == 123);
    REQUIRE(session.selection.getEnd() == 400);
    undo->redo();
    REQUIRE(session.getEditRevision() == recorded);
    REQUIRE(session.cursor == recorded->shape().frames);
}
TEST_CASE("Recording queue overflow retains a contiguous prefix",
          "[revision-recording]")
{
    Files files;
    storage::RecordingWriter writer(silent(0), 0, files.root / "audio");
    for (int64_t i = 0; i < storage::RecordingWriter::queueChunks; ++i)
    {
        REQUIRE(writer.submit(chunk(i * 256)));
    }
    REQUIRE_FALSE(
        writer.submit(chunk(storage::RecordingWriter::queueChunks * 256)));
    writer.startWorker();
    until(
        [&]
        {
            return writer.snapshot().completed;
        });
    const auto result = writer.snapshot();
    REQUIRE_FALSE(result.error.empty());
    REQUIRE(result.endFrame == storage::RecordingWriter::queueChunks * 256);
    REQUIRE(writer.peakQueuedChunks() == storage::RecordingWriter::queueChunks);
    REQUIRE(read(*result.audio, 1, result.endFrame - 1) ==
            float(result.endFrame % 127) / 128);
    REQUIRE_FALSE(writer.submit(chunk(result.endFrame)));
}
TEST_CASE("Recording failure leaves previously committed audio readable",
          "[revision-recording]")
{
    Files files;
    const bool invalidChunk = GENERATE(false, true);
    if (!invalidChunk)
    {
        std::filesystem::create_directories(files.root / "audio");
    }
    auto original = silent(100);
    storage::RecordingWriter writer(original, 0, files.root / "audio");
    writer.submit(chunk(1));
    writer.finish();
    writer.startWorker();
    until(
        [&]
        {
            return writer.snapshot().completed;
        });
    REQUIRE_FALSE(writer.snapshot().error.empty());
    REQUIRE(writer.snapshot().audio == original);
}
TEST_CASE("Recording completion rejects a replaced document",
          "[revision-recording]")
{
    Files files;
    State state;
    state.paths.reset();
    auto &session = state.getActiveDocumentSession();
    bind(session, silent(10000));
    actions::startRevisionRecording(&state, 0, files.root / "audio");
    auto recording = state.revisionRecording;
    recording->writer.submit(chunk(0));
    recording->writer.finish();
    recording->finishing = true;
    auto replacement = silent(30);
    bind(session, replacement);
    until(
        [&]
        {
            actions::pollRevisionRecording(&state);
            return !state.revisionRecording;
        });
    REQUIRE(session.getEditRevision() == replacement);
    REQUIRE(state.getActiveTab()->undoables.empty());
}
TEST_CASE(
    "Queued recording start is acknowledged even when stopped immediately",
    "[revision-recording]")
{
    State state;
    state.paths.reset();
    audio::AudioDevices devices(false);
    Document doc;
    doc.initialize(SampleFormat::FLOAT32, 48000, 2, 0);
    devices.enqueue(audio::Record{.document = &doc,
                                  .startPos = 0,
                                  .endPos = 0,
                                  .boundedToEnd = false,
                                  .vuMeter = nullptr});
    REQUIRE(devices.hasPendingRecordStart());
    devices.enqueue(audio::Stop{});
    devices.processCallbackCycle(nullptr, nullptr, 0);
    REQUIRE_FALSE(devices.hasPendingRecordStart());
    REQUIRE_FALSE(devices.isRecording());
}
TEST_CASE(
    "Callback chunks reach revision recording through the production drain",
    "[revision-recording]")
{
    Files files;
    State state;
    state.paths.reset();
    state.audioDevices = std::make_unique<audio::AudioDevices>(false);
    auto &session = state.getActiveDocumentSession();
    const auto format = GENERATE(SampleFormat::FLOAT32, SampleFormat::PCM_S16);
    actions::createNewDocument(&state, 48000, format, 2, false);
    REQUIRE(session.hasReadRevision());
    const auto empty = session.getEditRevision();
    actions::startRevisionRecording(&state, 0, files.root / "audio");
    state.audioDevices->enqueue(audio::Record{.document = &session.document,
                                              .startPos = 0,
                                              .endPos = 0,
                                              .boundedToEnd = false,
                                              .vuMeter = nullptr});
    // A tick before callback acknowledgement must not commit an empty
    // recording.
    REQUIRE_FALSE(actions::consumeRevisionRecordedAudio(&state));
    REQUIRE(state.revisionRecording);
    REQUIRE_FALSE(state.revisionRecording->finishing);
    auto captured = chunk(0);
    state.audioDevices->processCallbackCycle(captured.interleavedSamples.data(),
                                             nullptr, 256);
    actions::consumeRevisionRecordedAudio(&state);
    state.audioDevices->enqueue(audio::Stop{});
    state.audioDevices->processCallbackCycle(nullptr, nullptr, 0);
    until(
        [&]
        {
            actions::consumeRevisionRecordedAudio(&state);
            return !state.revisionRecording;
        });
    REQUIRE(session.document.getFrameCount() == 256);
    REQUIRE(state.getActiveTab()->undoables.size() == 1);
    for (int c = 0; c < 2; ++c)
    {
        for (int f = 0; f < 256; ++f)
        {
            REQUIRE(read(*session.getEditRevision(), c, f) ==
                    captured.interleavedSamples[f * 2 + c]);
        }
    }
    REQUIRE(session.revisionHasUnsavedChanges());
    REQUIRE_FALSE(session.undoStore.isAttached());
    const auto recorded = session.getEditRevision();
    state.undo();
    REQUIRE(session.getEditRevision() == empty);
    REQUIRE_FALSE(session.revisionHasUnsavedChanges());
    state.redo();
    REQUIRE(session.getEditRevision() == recorded);

    const auto checkpoint = files.root / "checkpoint";
    persistence::RevisionPersistence::save(
        checkpoint, *persistence::RevisionPersistence::capture(session, state.getActiveTab()));
    State restored;
    restored.paths.reset();
    auto &recovered = restored.getActiveDocumentSession();
    REQUIRE(persistence::loadDocumentAutosaveSnapshot(checkpoint, recovered));
    REQUIRE(persistence::RevisionPersistence::installHistory(&restored, 0));
    restored.undo();
    REQUIRE(recovered.document.getFrameCount() == 0);
    REQUIRE_FALSE(recovered.revisionHasUnsavedChanges());
    restored.redo();
    REQUIRE(recovered.document.getFrameCount() == 256);

    const auto output = files.root / "recorded.wav";
    const auto settings = *file::defaultExportSettingsForPath(output, format);
    REQUIRE(actions::io::queueSaveAs(&restored, output.string(), settings));
    until([&]
    {
        actions::io::processPendingSaveWork(&restored);
        return !restored.backgroundSaveJob;
    });
    REQUIRE(recovered.currentFile == output.string());
    REQUIRE_FALSE(recovered.revisionHasUnsavedChanges());
    const auto saved = file::loadAudioFile(output.string());
    REQUIRE(saved.document.getFrameCount() == 256);
    for (int c = 0; c < 2; ++c)
        for (int f = 0; f < 256; ++f)
            REQUIRE(std::abs(saved.document.getSample(c, f) -
                             captured.interleavedSamples[f * 2 + c]) <= 1.f / 32768);
}
TEST_CASE(
    "A failed recording segment rollover keeps earlier published blocks "
    "readable",
    "[revision-recording]")
{
    Files files;
    auto store = std::make_shared<storage::AudioBlockStore>(
        files.root / "audio", storage::AudioBlockBytes);
    auto cache = std::make_shared<storage::DecodedBlockCache>(0);
    const storage::AudioShape shape{storage::AudioBlockFrames, 1, 48000,
                                    SampleFormat::FLOAT32};
    storage::AudioRevisionBuilder first(shape, store, cache);
    std::vector<float> samples(storage::AudioBlockFrames, .375f);
    first.appendInterleaved(samples);
    const auto committed = first.finish();
    // An unwritable next segment simulates disk/write failure
    // deterministically.
    std::filesystem::create_directory(store->path() / "samples-1.bin");
    storage::AudioRevisionBuilder next(shape, store, cache);
    REQUIRE_THROWS(next.appendInterleaved(samples));
    REQUIRE_THROWS(next.finish());
    REQUIRE(read(*committed, 0, 0) == .375f);
    REQUIRE(read(*committed, 0, storage::AudioBlockFrames - 1) == .375f);
}
TEST_CASE(
    "Recording reports failure and commits only successfully published audio",
    "[revision-recording]")
{
    Files files;
    State state;
    state.paths.reset();
    std::string reported;
    state.errorReporter = [&](const auto &, const auto &message)
    {
        reported = message;
    };
    auto &session = state.getActiveDocumentSession();
    bind(session, silent(0));
    actions::startRevisionRecording(&state, 0, files.root / "audio");
    auto recording = state.revisionRecording;
    for (int64_t f = 0; f < 8192; f += 256)
    {
        REQUIRE(recording->writer.submit(chunk(f)));
    }
    REQUIRE(recording->writer.submit(chunk(8193))); // Reject a missing frame.
    recording->finishing = true;
    recording->writer.finish();
    until(
        [&]
        {
            actions::pollRevisionRecording(&state);
            return !state.revisionRecording;
        });
    REQUIRE_FALSE(reported.empty());
    REQUIRE(session.document.getFrameCount() == 8192);
    REQUIRE(state.getActiveTab()->undoables.size() == 1);
    REQUIRE(read(*session.getEditRevision(), 0, 8191) ==
            float(8191 % 127) / 128);
    state.getActiveTab()->undoables.back()->undo();
    REQUIRE(session.document.getFrameCount() == 0);
}
