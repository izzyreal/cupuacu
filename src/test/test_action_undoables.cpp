#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "gui/MainView.hpp"
#include "State.hpp"
#include "actions/audio/Cut.hpp"
#include "actions/audio/EditCommands.hpp"
#include "actions/audio/Paste.hpp"
#include "actions/audio/Trim.hpp"
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
