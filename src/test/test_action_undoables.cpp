#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "gui/MainView.hpp"
#include "State.hpp"
#include "actions/audio/Cut.hpp"
#include "actions/audio/EffectCommands.hpp"
#include "actions/audio/EditCommands.hpp"
#include "actions/audio/Paste.hpp"
#include "actions/audio/RecordedChunkApplier.hpp"
#include "actions/audio/SetSampleValue.hpp"
#include "actions/audio/Trim.hpp"
#include "audio/RecordedChunk.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/Window.hpp"

#include <memory>
#include <vector>

namespace
{
    void initializeMonoDocument(cupuacu::State &state,
                                const std::vector<float> &samples)
    {
        auto &document = state.activeDocumentSession.document;
        document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1,
                            static_cast<int64_t>(samples.size()));
        for (size_t i = 0; i < samples.size(); ++i)
        {
            document.setSample(0, static_cast<int64_t>(i), samples[i], false);
        }
    }

    std::vector<float> readMonoSamples(const cupuacu::Document &document)
    {
        std::vector<float> result(
            static_cast<size_t>(document.getFrameCount()));
        for (int64_t i = 0; i < document.getFrameCount(); ++i)
        {
            result[static_cast<size_t>(i)] = document.getSample(0, i);
        }
        return result;
    }

    void initializeStereoDocument(cupuacu::State &state,
                                  const std::vector<float> &leftSamples,
                                  const std::vector<float> &rightSamples)
    {
        REQUIRE(leftSamples.size() == rightSamples.size());

        auto &document = state.activeDocumentSession.document;
        document.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 2,
                            static_cast<int64_t>(leftSamples.size()));
        for (size_t i = 0; i < leftSamples.size(); ++i)
        {
            document.setSample(0, static_cast<int64_t>(i), leftSamples[i], false);
            document.setSample(1, static_cast<int64_t>(i), rightSamples[i], false);
        }
    }

    std::vector<float> readChannelSamples(const cupuacu::Document &document,
                                          const int64_t channel)
    {
        std::vector<float> result(
            static_cast<size_t>(document.getFrameCount()));
        for (int64_t i = 0; i < document.getFrameCount(); ++i)
        {
            result[static_cast<size_t>(i)] = document.getSample(channel, i);
        }
        return result;
    }
} // namespace

TEST_CASE("Cut undoable updates clipboard, cursor, and undo state", "[actions]")
{
    cupuacu::State state{};
    initializeMonoDocument(state, {0, 1, 2, 3, 4, 5});

    auto &session = state.activeDocumentSession;
    session.selection.setValue1(2.0);
    session.selection.setValue2(5.0);
    session.cursor = 5;

    cupuacu::actions::audio::performCut(&state);

    REQUIRE(state.undoables.size() == 1);
    REQUIRE(session.document.getFrameCount() == 3);
    REQUIRE(readMonoSamples(session.document) ==
            std::vector<float>({0, 1, 5}));
    REQUIRE(state.clipboard.getFrameCount() == 3);
    REQUIRE(readMonoSamples(state.clipboard) ==
            std::vector<float>({2, 3, 4}));
    REQUIRE_FALSE(session.selection.isActive());
    REQUIRE(session.cursor == 2);

    state.undo();

    REQUIRE(session.document.getFrameCount() == 6);
    REQUIRE(readMonoSamples(session.document) ==
            std::vector<float>({0, 1, 2, 3, 4, 5}));
    REQUIRE(session.selection.isActive());
    REQUIRE(session.selection.getStartInt() == 2);
    REQUIRE(session.selection.getLengthInt() == 3);
    REQUIRE(session.cursor == 5);

    state.redo();
    REQUIRE(readMonoSamples(session.document) ==
            std::vector<float>({0, 1, 5}));
}

TEST_CASE("Paste insert undoable inserts clipboard content and restores on undo",
          "[actions]")
{
    cupuacu::State state{};
    initializeMonoDocument(state, {0, 1, 2, 3, 4});
    state.clipboard.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, 2);
    state.clipboard.setSample(0, 0, 9.0f, false);
    state.clipboard.setSample(0, 1, 8.0f, false);

    auto &session = state.activeDocumentSession;
    session.cursor = 4;

    state.addAndDoUndoable(
        std::make_shared<cupuacu::actions::audio::Paste>(&state, 2, -1));

    REQUIRE(readMonoSamples(session.document) ==
            std::vector<float>({0, 1, 9, 8, 2, 3, 4}));
    REQUIRE(session.selection.isActive());
    REQUIRE(session.selection.getStartInt() == 2);
    REQUIRE(session.selection.getLengthInt() == 2);
    REQUIRE(session.cursor == 2);

    state.undo();

    REQUIRE(readMonoSamples(session.document) ==
            std::vector<float>({0, 1, 2, 3, 4}));
    REQUIRE_FALSE(session.selection.isActive());
    REQUIRE(session.cursor == 4);
}

TEST_CASE("Paste overwrite undoable replaces selected range and restores it",
          "[actions]")
{
    cupuacu::State state{};
    initializeMonoDocument(state, {0, 1, 2, 3, 4, 5});
    state.clipboard.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, 2);
    state.clipboard.setSample(0, 0, 30.0f, false);
    state.clipboard.setSample(0, 1, 40.0f, false);

    auto &session = state.activeDocumentSession;
    session.selection.setValue1(2.0);
    session.selection.setValue2(5.0);
    session.cursor = 1;

    state.addAndDoUndoable(
        std::make_shared<cupuacu::actions::audio::Paste>(&state, 2, 5));

    REQUIRE(readMonoSamples(session.document) ==
            std::vector<float>({0, 1, 30, 40, 5}));
    REQUIRE(session.selection.isActive());
    REQUIRE(session.selection.getStartInt() == 2);
    REQUIRE(session.selection.getLengthInt() == 2);
    REQUIRE(session.cursor == 2);

    state.undo();

    REQUIRE(readMonoSamples(session.document) ==
            std::vector<float>({0, 1, 2, 3, 4, 5}));
    REQUIRE(session.selection.isActive());
    REQUIRE(session.selection.getStartInt() == 2);
    REQUIRE(session.selection.getLengthInt() == 3);
    REQUIRE(session.cursor == 1);
}

TEST_CASE("Trim undoable keeps requested middle range and restores document",
          "[actions]")
{
    cupuacu::State state{};
    initializeMonoDocument(state, {0, 1, 2, 3, 4, 5});

    state.addAndDoUndoable(
        std::make_shared<cupuacu::actions::audio::Trim>(&state, 1, 3));

    auto &session = state.activeDocumentSession;
    REQUIRE(readMonoSamples(session.document) == std::vector<float>({1, 2, 3}));
    REQUIRE(session.selection.isActive());
    REQUIRE(session.selection.getStartInt() == 0);
    REQUIRE(session.selection.getLengthInt() == 3);
    REQUIRE(session.cursor == 0);

    state.undo();

    REQUIRE(readMonoSamples(session.document) ==
            std::vector<float>({0, 1, 2, 3, 4, 5}));
    REQUIRE(session.selection.isActive());
    REQUIRE(session.selection.getStartInt() == 1);
    REQUIRE(session.selection.getLengthInt() == 3);
    REQUIRE(session.cursor == 1);
}

TEST_CASE("Cut at document tail removes trailing frames and restores on undo",
          "[actions]")
{
    cupuacu::State state{};
    initializeMonoDocument(state, {0, 1, 2, 3, 4, 5});

    auto &session = state.activeDocumentSession;
    session.selection.setValue1(4.0);
    session.selection.setValue2(6.0);
    session.cursor = 5;

    cupuacu::actions::audio::performCut(&state);

    REQUIRE(readMonoSamples(session.document) ==
            std::vector<float>({0, 1, 2, 3}));
    REQUIRE(readMonoSamples(state.clipboard) ==
            std::vector<float>({4, 5}));
    REQUIRE(session.cursor == 4);
    REQUIRE_FALSE(session.selection.isActive());

    state.undo();
    REQUIRE(readMonoSamples(session.document) ==
            std::vector<float>({0, 1, 2, 3, 4, 5}));
    REQUIRE(session.selection.isActive());
    REQUIRE(session.selection.getStartInt() == 4);
    REQUIRE(session.selection.getLengthInt() == 2);
    REQUIRE(session.cursor == 5);
}

TEST_CASE("Paste insert at document end appends clipboard content", "[actions]")
{
    cupuacu::State state{};
    initializeMonoDocument(state, {0, 1, 2});
    state.clipboard.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, 2);
    state.clipboard.setSample(0, 0, 7.0f, false);
    state.clipboard.setSample(0, 1, 8.0f, false);

    auto &session = state.activeDocumentSession;
    session.cursor = 3;

    cupuacu::actions::audio::performPaste(&state);

    REQUIRE(readMonoSamples(session.document) ==
            std::vector<float>({0, 1, 2, 7, 8}));
    REQUIRE(session.selection.isActive());
    REQUIRE(session.selection.getStartInt() == 3);
    REQUIRE(session.selection.getLengthInt() == 2);
    REQUIRE(session.cursor == 3);

    state.undo();
    REQUIRE(readMonoSamples(session.document) == std::vector<float>({0, 1, 2}));
    REQUIRE_FALSE(session.selection.isActive());
    REQUIRE(session.cursor == 3);
}

TEST_CASE("Paste with empty clipboard is a no-op", "[actions]")
{
    cupuacu::State state{};
    initializeMonoDocument(state, {0, 1, 2});

    auto &session = state.activeDocumentSession;
    session.cursor = 1;
    cupuacu::actions::audio::performPaste(&state);

    REQUIRE(readMonoSamples(session.document) == std::vector<float>({0, 1, 2}));
    REQUIRE(state.undoables.size() == 1);
    REQUIRE_FALSE(session.selection.isActive());
    REQUIRE(session.cursor == 1);

    state.undo();
    REQUIRE(readMonoSamples(session.document) == std::vector<float>({0, 1, 2}));
    REQUIRE_FALSE(session.selection.isActive());
    REQUIRE(session.cursor == 1);
}

TEST_CASE("Trim full document keeps content and restores selection on undo",
          "[actions]")
{
    cupuacu::State state{};
    initializeMonoDocument(state, {0, 1, 2, 3});

    auto &session = state.activeDocumentSession;
    session.selection.setValue1(0.0);
    session.selection.setValue2(4.0);
    session.cursor = 3;

    cupuacu::actions::audio::performTrim(&state);

    REQUIRE(readMonoSamples(session.document) == std::vector<float>({0, 1, 2, 3}));
    REQUIRE(session.selection.isActive());
    REQUIRE(session.selection.getStartInt() == 0);
    REQUIRE(session.selection.getLengthInt() == 4);
    REQUIRE(session.cursor == 0);

    state.undo();
    REQUIRE(readMonoSamples(session.document) == std::vector<float>({0, 1, 2, 3}));
    REQUIRE(session.selection.isActive());
    REQUIRE(session.selection.getStartInt() == 0);
    REQUIRE(session.selection.getLengthInt() == 4);
    REQUIRE(session.cursor == 0);
}

TEST_CASE("Cut and trim without active selection are no-ops", "[actions]")
{
    cupuacu::State state{};
    initializeMonoDocument(state, {0, 1, 2, 3});

    auto &session = state.activeDocumentSession;
    session.selection.reset();
    session.cursor = 2;

    cupuacu::actions::audio::performCut(&state);
    cupuacu::actions::audio::performTrim(&state);

    REQUIRE(readMonoSamples(session.document) == std::vector<float>({0, 1, 2, 3}));
    REQUIRE(state.undoables.empty());
    REQUIRE_FALSE(session.selection.isActive());
    REQUIRE(session.cursor == 2);
}

TEST_CASE("Copy undoable preserves zero-based selection and restores it on undo",
          "[actions]")
{
    cupuacu::State state{};
    initializeMonoDocument(state, {0, 1, 2, 3});

    auto &session = state.activeDocumentSession;
    session.selection.setValue1(0.0);
    session.selection.setValue2(2.0);
    session.cursor = 3;

    cupuacu::actions::audio::performCopy(&state);

    REQUIRE(state.undoables.size() == 1);
    REQUIRE(readMonoSamples(session.document) == std::vector<float>({0, 1, 2, 3}));
    REQUIRE(readMonoSamples(state.clipboard) == std::vector<float>({0, 1}));
    REQUIRE(session.selection.isActive());
    REQUIRE(session.selection.getStartInt() == 0);
    REQUIRE(session.selection.getLengthInt() == 2);
    REQUIRE(session.cursor == 0);

    session.selection.reset();
    session.cursor = 1;
    state.undo();

    REQUIRE(session.selection.isActive());
    REQUIRE(session.selection.getStartInt() == 0);
    REQUIRE(session.selection.getLengthInt() == 2);
    REQUIRE(session.cursor == 3);
}

TEST_CASE("Paste overwrite undo restores zero-based previous selection",
          "[actions]")
{
    cupuacu::State state{};
    initializeMonoDocument(state, {0, 1, 2, 3});
    state.clipboard.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, 1);
    state.clipboard.setSample(0, 0, 9.0f, false);

    auto &session = state.activeDocumentSession;
    session.selection.setValue1(0.0);
    session.selection.setValue2(2.0);
    session.cursor = 2;

    auto undoable =
        std::make_shared<cupuacu::actions::audio::Paste>(&state, 0, 2);
    state.addAndDoUndoable(undoable);
    state.undo();

    REQUIRE(session.selection.isActive());
    REQUIRE(session.selection.getStartInt() == 0);
    REQUIRE(session.selection.getLengthInt() == 2);
    REQUIRE(session.cursor == 2);
}

TEST_CASE("SetSampleValue undoable changes one sample and restores it", "[actions]")
{
    cupuacu::State state{};
    initializeMonoDocument(state, {0, 1, 2});

    auto undoable = std::make_shared<cupuacu::actions::audio::SetSampleValue>(
        &state, 0, 1, 1.0f);
    undoable->setNewValue(42.0f);
    state.addAndDoUndoable(undoable);

    REQUIRE(readMonoSamples(state.activeDocumentSession.document) ==
            std::vector<float>({0, 42, 2}));

    state.undo();
    REQUIRE(readMonoSamples(state.activeDocumentSession.document) ==
            std::vector<float>({0, 1, 2}));

    state.redo();
    REQUIRE(readMonoSamples(state.activeDocumentSession.document) ==
            std::vector<float>({0, 42, 2}));
}

TEST_CASE("Amplify/Fade applies across the whole document on all channels",
          "[actions]")
{
    cupuacu::State state{};
    initializeStereoDocument(state, {1, 2, 3}, {4, 5, 6});

    cupuacu::actions::audio::performAmplifyFade(&state, 200.0, 200.0, 0);

    REQUIRE(readChannelSamples(state.activeDocumentSession.document, 0) ==
            std::vector<float>({2, 4, 6}));
    REQUIRE(readChannelSamples(state.activeDocumentSession.document, 1) ==
            std::vector<float>({8, 10, 12}));

    state.undo();

    REQUIRE(readChannelSamples(state.activeDocumentSession.document, 0) ==
            std::vector<float>({1, 2, 3}));
    REQUIRE(readChannelSamples(state.activeDocumentSession.document, 1) ==
            std::vector<float>({4, 5, 6}));
}

TEST_CASE("Amplify/Fade limits selected stereo edits to the chosen channel",
          "[actions]")
{
    cupuacu::State state{};
    initializeStereoDocument(state, {1, 2, 3, 4}, {10, 20, 30, 40});
    state.activeDocumentSession.selection.setHighest(4.0);
    state.activeDocumentSession.selection.setValue1(1.0);
    state.activeDocumentSession.selection.setValue2(3.0);

    state.mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            &state, &state.activeDocumentSession, "main", 640, 360,
            SDL_WINDOW_HIDDEN);
    state.mainDocumentSessionWindow->getViewState().selectedChannels =
        cupuacu::SelectedChannels::RIGHT;

    cupuacu::actions::audio::performAmplifyFade(&state, 50.0, 50.0, 0);

    REQUIRE(readChannelSamples(state.activeDocumentSession.document, 0) ==
            std::vector<float>({1, 2, 3, 4}));
    REQUIRE(readChannelSamples(state.activeDocumentSession.document, 1) ==
            std::vector<float>({10, 10, 15, 40}));

    state.undo();

    REQUIRE(readChannelSamples(state.activeDocumentSession.document, 1) ==
            std::vector<float>({10, 20, 30, 40}));
}

TEST_CASE("Amplify/Fade processes the full selected range and stops at selection end",
          "[actions]")
{
    cupuacu::State state{};
    initializeMonoDocument(state, {10, 10, 10, 10, 10, 10});
    state.activeDocumentSession.selection.setHighest(6.0);
    state.activeDocumentSession.selection.setValue1(1.0);
    state.activeDocumentSession.selection.setValue2(5.0);

    cupuacu::actions::audio::performAmplifyFade(&state, 100.0, 0.0, 0);

    REQUIRE(readMonoSamples(state.activeDocumentSession.document)[0] ==
            Catch::Approx(10.0f));
    REQUIRE(readMonoSamples(state.activeDocumentSession.document)[1] ==
            Catch::Approx(10.0f));
    REQUIRE(readMonoSamples(state.activeDocumentSession.document)[2] ==
            Catch::Approx(10.0f * (2.0f / 3.0f)));
    REQUIRE(readMonoSamples(state.activeDocumentSession.document)[3] ==
            Catch::Approx(10.0f * (1.0f / 3.0f)));
    REQUIRE(readMonoSamples(state.activeDocumentSession.document)[4] ==
            Catch::Approx(0.0f));
    REQUIRE(readMonoSamples(state.activeDocumentSession.document)[5] ==
            Catch::Approx(10.0f));
}

TEST_CASE("Normalize percent reflects the peak of the selected target range",
          "[actions]")
{
    cupuacu::State state{};
    initializeStereoDocument(state, {0.25f, 0.5f, 0.75f}, {0.1f, 0.2f, 0.3f});
    state.activeDocumentSession.selection.setHighest(3.0);
    state.activeDocumentSession.selection.setValue1(1.0);
    state.activeDocumentSession.selection.setValue2(3.0);

    state.mainDocumentSessionWindow =
        std::make_unique<cupuacu::gui::DocumentSessionWindow>(
            &state, &state.activeDocumentSession, "main", 640, 360,
            SDL_WINDOW_HIDDEN);
    state.mainDocumentSessionWindow->getViewState().selectedChannels =
        cupuacu::SelectedChannels::RIGHT;

    REQUIRE(cupuacu::actions::audio::computeNormalizePercent(&state) ==
            Catch::Approx(333.333333).epsilon(0.001));
}

TEST_CASE("Dynamics compresses selected samples and respects undo", "[actions]")
{
    cupuacu::State state{};
    initializeMonoDocument(state, {0.2f, 0.5f, 0.9f, -1.0f});

    state.activeDocumentSession.selection.setHighest(4.0);
    state.activeDocumentSession.selection.setValue1(1.0);
    state.activeDocumentSession.selection.setValue2(4.0);

    cupuacu::actions::audio::performDynamics(&state, 50.0, 1);

    const auto processed = readMonoSamples(state.activeDocumentSession.document);
    REQUIRE(processed[0] == Catch::Approx(0.2f));
    REQUIRE(processed[1] == Catch::Approx(0.5f));
    REQUIRE(processed[2] == Catch::Approx(0.6f));
    REQUIRE(processed[3] == Catch::Approx(-0.625f));

    state.undo();
    REQUIRE(readMonoSamples(state.activeDocumentSession.document) ==
            std::vector<float>({0.2f, 0.5f, 0.9f, -1.0f}));
}

TEST_CASE("Recorded chunk applier initializes empty document from chunk", "[actions]")
{
    cupuacu::Document doc;
    cupuacu::audio::RecordedChunk chunk{};
    chunk.startFrame = 0;
    chunk.frameCount = 3;
    chunk.channelCount = 2;
    chunk.interleavedSamples[0] = 1.0f;
    chunk.interleavedSamples[1] = -1.0f;
    chunk.interleavedSamples[2] = 0.5f;
    chunk.interleavedSamples[3] = -0.5f;
    chunk.interleavedSamples[4] = 0.25f;
    chunk.interleavedSamples[5] = -0.25f;

    const auto result =
        cupuacu::actions::audio::applyRecordedChunk(doc, chunk);

    REQUIRE(result.channelLayoutChanged);
    REQUIRE(result.waveformCacheChanged);
    REQUIRE(result.requiredFrameCount == 3);
    REQUIRE(doc.getChannelCount() == 2);
    REQUIRE(doc.getFrameCount() == 3);
    REQUIRE(doc.getSample(0, 0) == Catch::Approx(1.0f));
    REQUIRE(doc.getSample(1, 0) == Catch::Approx(-1.0f));
    REQUIRE(doc.getSample(0, 2) == Catch::Approx(0.25f));
    REQUIRE(doc.getSample(1, 2) == Catch::Approx(-0.25f));
}

TEST_CASE("Recorded chunk applier expands channel layout and appends frames",
          "[actions]")
{
    cupuacu::Document doc;
    doc.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, 2);
    doc.setSample(0, 0, 10.0f, false);
    doc.setSample(0, 1, 20.0f, false);

    cupuacu::audio::RecordedChunk chunk{};
    chunk.startFrame = 1;
    chunk.frameCount = 3;
    chunk.channelCount = 2;
    chunk.interleavedSamples[0] = 1.0f;
    chunk.interleavedSamples[1] = 2.0f;
    chunk.interleavedSamples[2] = 3.0f;
    chunk.interleavedSamples[3] = 4.0f;
    chunk.interleavedSamples[4] = 5.0f;
    chunk.interleavedSamples[5] = 6.0f;

    const auto result =
        cupuacu::actions::audio::applyRecordedChunk(doc, chunk);

    REQUIRE(result.channelLayoutChanged);
    REQUIRE(result.waveformCacheChanged);
    REQUIRE(result.requiredFrameCount == 4);
    REQUIRE(doc.getChannelCount() == 2);
    REQUIRE(doc.getFrameCount() == 4);
    REQUIRE(doc.getSample(0, 0) == Catch::Approx(10.0f));
    REQUIRE(doc.getSample(0, 1) == Catch::Approx(1.0f));
    REQUIRE(doc.getSample(1, 1) == Catch::Approx(2.0f));
    REQUIRE(doc.getSample(0, 3) == Catch::Approx(5.0f));
    REQUIRE(doc.getSample(1, 3) == Catch::Approx(6.0f));
}
