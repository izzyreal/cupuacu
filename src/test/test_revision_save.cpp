#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "TestPaths.hpp"
#include "file/OwnedAudioImport.hpp"
#include "file/RevisionPreservationWriter.hpp"
#include "file/wav/WavParser.hpp"
#include "file/aiff/AiffParser.hpp"
#include "file/wav/WavMarkerMetadata.hpp"
#include "file/aiff/AiffMarkerMetadata.hpp"
#include "actions/io/BackgroundSave.hpp"
#include "actions/DocumentUi.hpp"
#include "LongTask.hpp"
#include <sndfile.h>
#include <fstream>

using namespace cupuacu;
namespace
{
    struct Files
    {
        std::filesystem::path root = test::makeUniqueTestRoot("revision-save");
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
    int original(int64_t frame, int channel, int seed = 0)
    {
        // Deliberately retains low bits lost by float decoding.
        return int((uint32_t(frame) * 971 + channel * 313 + seed * 37) %
                   0x20000000) +
               0x20000001;
    }
    void fixture(const std::filesystem::path &path, bool wav, int frames,
                 int seed = 0)
    {
        SF_INFO info{};
        info.channels = 2;
        info.samplerate = 48000;
        info.format = (wav ? SF_FORMAT_WAV : SF_FORMAT_AIFF) | SF_FORMAT_PCM_32;
        auto snd = sf_open(path.string().c_str(), SFM_WRITE, &info);
        REQUIRE(snd);
        std::vector<int> samples(frames * 2);
        for (int i = 0; i < frames; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                samples[i * 2 + c] = original(i, c, seed);
            }
        }
        REQUIRE(sf_writef_int(snd, samples.data(), frames) == frames);
        REQUIRE(sf_close(snd) == 0);
    }
    auto import(const std::filesystem::path &path,
                const std::filesystem::path &root)
    {
        return file::importOwnedAudio(
            path, root,
            std::make_shared<storage::DecodedBlockCache>(
                storage::AudioBlockBytes));
    }
    std::vector<int> read(const std::filesystem::path &path)
    {
        SF_INFO info{};
        auto snd = sf_open(path.string().c_str(), SFM_READ, &info);
        REQUIRE(snd);
        std::vector<int> result(info.frames * info.channels);
        REQUIRE(sf_readf_int(snd, result.data(), info.frames) == info.frames);
        REQUIRE(sf_close(snd) == 0);
        return result;
    }
    std::string bytes(const std::filesystem::path &path)
    {
        std::ifstream in(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), {}};
    }
    void drain(State &state)
    {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (state.backgroundSaveJob &&
               std::chrono::steady_clock::now() < deadline)
        {
            actions::io::processPendingSaveWork(&state);
            std::this_thread::yield();
        }
        REQUIRE_FALSE(state.backgroundSaveJob);
    }
} // namespace
TEST_CASE(
    "Revision preservation copies exact PCM32 across edits and owned sources",
    "[revision-save]")
{
    const bool wav = GENERATE(false, true);
    Files files;
    const auto path = files.root / (wav ? "input.wav" : "input.aiff");
    const auto other = files.root / (wav ? "other.aiff" : "other.wav");
    constexpr int frames = 65573;
    fixture(path, wav, frames);
    fixture(other, !wav, frames, 7);
    auto a = import(path, files.root / "a");
    auto b = import(other, files.root / "b");
    auto first = storage::AudioEditRevision::from(a.audio);
    auto second = storage::AudioEditRevision::from(b.audio);
    storage::AudioEditTransaction edit(*first);
    edit.erase(3, 5);
    edit.replaceChannel(1, 65530, 7, second.get(), 0, 65531);
    edit.replaceChannel(0, 42, 1, nullptr, 0, 0, .5f);
    auto audio = edit.finish();
    const auto settings =
        *file::defaultExportSettingsForPath(path, SampleFormat::PCM_S32);
    const auto io = a.audio->blockStore()->ioBytes();
    // The user source can disappear; all original bytes are independently
    // owned.
    std::filesystem::remove(path);
    std::filesystem::remove(other);
    std::vector<DocumentMarker> markers{{1, 10, "retained marker"}};
    file::writePreservingRevision(*audio, markers, a.audio->sourcePath(), path,
                                  settings);
    REQUIRE(a.audio->blockStore()->ioBytes() ==
            io); // No float sample-file reads.
    auto samples = read(path);
    REQUIRE(samples.size() == (frames - 5) * 2);
    for (int i = 0; i < frames - 5; ++i)
    {
        const int source = i < 3 ? i : i + 5;
        REQUIRE(samples[i * 2] == (i == 42 ? 1073741824 : original(source, 0)));
        REQUIRE(samples[i * 2 + 1] == (i >= 65530 && i < 65537
                                           ? original(65531 + i - 65530, 0, 7)
                                           : original(source, 1)));
    }
    auto loadedMarkers = wav ? file::wav::markers::readMarkers(path)
                             : file::aiff::markers::readMarkers(path);
    REQUIRE(loadedMarkers.size() == 1);
    REQUIRE(loadedMarkers[0].frame == 10);
    const auto before = bytes(path);
    file::writePreservingRevision(*audio, markers, a.audio->sourcePath(), path,
                                  settings);
    REQUIRE(bytes(path) ==
            before); // Repeated overwrite keeps the original precision.
    int progressCalls = 0;
    REQUIRE_THROWS_AS(
        file::writePreservingRevision(*audio, markers, a.audio->sourcePath(),
                                      path, settings,
                                      [&](const auto &, auto)
                                      {
                                          if (++progressCalls == 4)
                                          {
                                              throw LongTaskCanceledError{};
                                          }
                                      }),
        LongTaskCanceledError);
    REQUIRE(bytes(path) == before);
    REQUIRE(progressCalls == 4);
    for (const auto &entry : std::filesystem::directory_iterator(files.root))
    {
        REQUIRE(entry.path().filename().string().find(".tmp") ==
                std::string::npos);
    }
}
TEST_CASE(
    "Background revision saves pin audio and finalize the originating tab",
    "[revision-save]")
{
    const bool preserving = GENERATE(false, true);
    Files files;
    const auto source = files.root / "source.wav",
               output = files.root / "output.wav";
    fixture(source, true, 1000);
    test::StateWithTestPaths state{files.root / "state"};
    state.tabs.resize(2);
    auto a = import(source, files.root / "audio");
    auto &session = state.tabs[0].session;
    session.document = std::move(a.metadata.document);
    const auto settings =
        *file::defaultExportSettingsForPath(output, SampleFormat::PCM_S32);
    session.setCurrentFile(source.string(), settings);
    auto before = storage::AudioEditRevision::from(a.audio);
    session.bindReadRevision(before);
    REQUIRE_FALSE(actions::documentSessionHasUnsavedChanges(session));
    session.autosaveSnapshotPath = files.root / "legacy-autosave";
    actions::io::queueAutosaveForTab(&state, 0);
    REQUIRE_FALSE(state.backgroundAutosaveJob);
    actions::io::processPendingAutosaveWork(&state);
    REQUIRE_FALSE(state.backgroundAutosaveJob);
    session.autosaveSnapshotPath.clear();
    state.tabs[1].session.currentFile = "other tab";
    std::string error;
    state.errorReporter = [&](auto, auto message)
    {
        error = message;
    };
    REQUIRE((preserving ? actions::io::queueSaveAsPreserving(
                              &state, output.string(), settings)
                        : actions::io::queueSaveAs(&state, output.string(),
                                                   settings)));
    storage::AudioEditTransaction edit(*before);
    edit.erase(0, 10);
    auto newer = edit.finish();
    REQUIRE(session.commitEditRevision(before, newer, {}));
    state.activeTabIndex = 1; // Completion must not change this tab.
    drain(state);
    REQUIRE(error.empty());
    REQUIRE(session.getEditRevision() == newer);
    REQUIRE(session.currentFile == output.string());
    REQUIRE(actions::documentSessionHasUnsavedChanges(session));
    REQUIRE(state.tabs[1].session.currentFile == "other tab");
    REQUIRE(state.activeTabIndex == 1);
    REQUIRE(read(output).size() == 2000);
    if (preserving)
    {
        REQUIRE(read(output)[0] == original(0, 0));
    }
    REQUIRE(session.commitEditRevision(newer, before, {}));
    REQUIRE_FALSE(actions::documentSessionHasUnsavedChanges(session));
    state.activeTabIndex = 0;
    REQUIRE(actions::io::queueOverwritePreserving(&state));
    drain(state);
    REQUIRE(error.empty());
    REQUIRE(session.getEditRevision() == before);
    REQUIRE_FALSE(actions::documentSessionHasUnsavedChanges(session));
    REQUIRE(read(output)[0] == original(0, 0));
}
TEST_CASE("Closed save targets cannot finalize a different document",
          "[revision-save]")
{
    Files files;
    const auto source = files.root / "source.wav",
               output = files.root / "output.wav";
    fixture(source, true, 1000);
    test::StateWithTestPaths state{files.root / "state"};
    auto a = import(source, files.root / "audio");
    auto &session = state.getActiveDocumentSession();
    session.document = std::move(a.metadata.document);
    session.bindReadRevision(storage::AudioEditRevision::from(a.audio));
    const auto settings =
        *file::defaultExportSettingsForPath(output, SampleFormat::PCM_S32);
    REQUIRE(actions::io::queueSaveAs(&state, output.string(), settings));
    state.tabs.clear();
    state.tabs.emplace_back();
    state.getActiveDocumentSession().currentFile = "replacement";
    drain(state);
    REQUIRE(state.getActiveDocumentSession().currentFile == "replacement");
    REQUIRE(read(output).size() == 2000);
}
TEST_CASE(
    "Revision preservation streams all PCM widths and retains opaque chunks",
    "[revision-save]")
{
    const bool wav = GENERATE(false, true);
    const auto format = GENERATE(SampleFormat::PCM_S8, SampleFormat::PCM_S16,
                                 SampleFormat::PCM_S24, SampleFormat::FLOAT32);
    Files files;
    const auto source = files.root / (wav ? "source.wav" : "source.aiff");
    const auto output = files.root / (wav ? "saved.wav" : "saved.aiff");
    Document document;
    document.initialize(format, 48000, 1, 16391);
    for (int64_t i = 0; i < 16391; ++i)
    {
        document.setSample(0, i, float(i % 117 - 58) / 128, false);
    }
    auto settings = *file::defaultExportSettingsForPath(source, format);
    settings.subtype = format == SampleFormat::PCM_S8
                           ? (wav ? SF_FORMAT_PCM_U8 : SF_FORMAT_PCM_S8)
                       : format == SampleFormat::PCM_S16 ? SF_FORMAT_PCM_16
                       : format == SampleFormat::PCM_S24 ? SF_FORMAT_PCM_24
                                                         : SF_FORMAT_FLOAT;
    file::AudioFileWriter::writeFile(document.acquireReadLease(), source,
                                     settings);
    auto put32 = [&](std::ostream &out, uint32_t value)
    {
        std::array<char, 4> v;
        for (int i = 0; i < 4; ++i)
        {
            v[wav ? i : 3 - i] = char(value >> (8 * i));
        }
        out.write(v.data(), v.size());
    };
    {
        std::fstream io(source,
                        std::ios::binary | std::ios::in | std::ios::out);
        io.seekp(0, std::ios::end);
        io.write("JUNK", 4);
        put32(io, 5);
        io.write("abcdeX", 6);
        const auto end = io.tellp();
        io.seekp(4);
        put32(io, uint32_t(end) - 8);
    }
    CAPTURE(wav, int(format), settings.subtype);
    auto expected = read(source);
    expected.erase(expected.begin() + 3);
    auto a = import(source, files.root / "audio");
    storage::AudioEditTransaction edit(
        *storage::AudioEditRevision::from(a.audio));
    edit.erase(3, 1);
    file::writePreservingRevision(*edit.finish(), {}, a.audio->sourcePath(),
                                  output, settings);
    REQUIRE(read(output) == expected);
    const auto encoded = bytes(output);
    auto payload = [&](const std::filesystem::path &path)
    {
        const auto fileBytes = bytes(path);
        if (wav)
        {
            const auto parsed = file::wav::WavParser::parseFile(path);
            const auto chunk = parsed.findChunk("data");
            return fileBytes.substr(chunk->payloadOffset, chunk->payloadSize);
        }
        const auto parsed = file::aiff::AiffParser::parseFile(path);
        return fileBytes.substr(parsed.soundDataOffset,
                                uint64_t(parsed.sampleFrameCount) *
                                    parsed.channelCount *
                                    (parsed.bitsPerSample / 8));
    };
    const int width = format == SampleFormat::PCM_S8    ? 1
                      : format == SampleFormat::PCM_S16 ? 2
                      : format == SampleFormat::PCM_S24 ? 3
                                                        : 4;
    auto originalPayload = payload(source);
    originalPayload.erase(3 * width, width);
    REQUIRE(payload(output) == originalPayload);
    REQUIRE(encoded.find("abcdeX") !=
            std::string::npos); // Includes original odd-chunk padding.
    if (wav)
    {
        const auto parsed = file::wav::WavParser::parseFile(output);
        if (const auto fact = parsed.findChunk("fact"))
        {
            uint32_t frames = 0;
            for (int i = 0; i < 4; ++i)
            {
                frames |= uint32_t(uint8_t(encoded[fact->payloadOffset + i]))
                          << (8 * i);
            }
            REQUIRE(frames == 16390);
        }
    }
    auto huge = storage::AudioEditRevision::silence(
        {int64_t(UINT32_MAX) + 1, 1, 48000, format});
    REQUIRE_THROWS_AS(file::writePreservingRevision(
                          *huge, {}, a.audio->sourcePath(), output, settings),
                      std::length_error);
    REQUIRE(bytes(output) == encoded);
}
TEST_CASE(
    "A save retains its container after every original sample and the tab are "
    "removed",
    "[revision-save]")
{
    Files files;
    const auto source = files.root / "source.wav",
               output = files.root / "output.wav";
    fixture(source, true, 1000);
    test::StateWithTestPaths state{files.root / "state"};
    auto a = import(source, files.root / "audio");
    auto &session = state.getActiveDocumentSession();
    session.document = std::move(a.metadata.document);
    session.bindReadRevision(storage::AudioEditRevision::from(a.audio));
    auto silent = storage::AudioEditRevision::silence(a.audio->shape());
    REQUIRE(session.commitEditRevision(session.getEditRevision(), silent, {}));
    const auto settings =
        *file::defaultExportSettingsForPath(output, SampleFormat::PCM_S32);
    state.backgroundSaveJob.reset(new actions::io::BackgroundSaveJob(
        1,
        actions::io::BackgroundSaveRequest{
            actions::io::BackgroundSaveKind::SaveAsPreserving, output,
            a.audio->sourcePath(), settings},
        &state, session.document, std::filesystem::path{}, silent));
    a.audio.reset();
    state.tabs.clear();
    state.tabs.emplace_back();
    std::filesystem::remove(source);
    state.backgroundSaveJob->start();
    drain(state);
    auto samples = read(output);
    REQUIRE(samples == std::vector<int>(2000, 0));
    REQUIRE(state.getActiveDocumentSession().currentFile.empty());
}
TEST_CASE(
    "Foreign unsupported raw encodings use decoded samples at the target "
    "precision",
    "[revision-save]")
{
    Files files;
    const auto source = files.root / "source.wav",
               other = files.root / "double.wav",
               output = files.root / "output.wav";
    fixture(source, true, 100);
    SF_INFO info{};
    info.channels = 2;
    info.samplerate = 48000;
    info.format = SF_FORMAT_WAV | SF_FORMAT_DOUBLE;
    auto snd = sf_open(other.string().c_str(), SFM_WRITE, &info);
    REQUIRE(snd);
    std::array<double, 10> values;
    values.fill(.5);
    REQUIRE(sf_writef_double(snd, values.data(), 5) == 5);
    REQUIRE(sf_close(snd) == 0);
    auto a = import(source, files.root / "a"),
         b = import(other, files.root / "b");
    auto foreign = storage::AudioEditRevision::from(b.audio)->forPaste(a.audio->shape());
    storage::AudioEditTransaction edit(
        *storage::AudioEditRevision::from(a.audio));
    edit.replaceChannel(0, 1, 5, foreign.get(), 0, 0);
    const auto settings =
        *file::defaultExportSettingsForPath(output, SampleFormat::PCM_S32);
    file::writePreservingRevision(*edit.finish(), {}, a.audio->sourcePath(),
                                  output, settings);
    const auto samples = read(output);
    REQUIRE(samples.size() == 200);
    for (int i = 0; i < 100; ++i)
    {
        REQUIRE(samples[i * 2] ==
                (i >= 1 && i < 6 ? 1073741824 : original(i, 0)));
        REQUIRE(samples[i * 2 + 1] == original(i, 1));
    }
}
