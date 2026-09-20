#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "IntegrationTestHelpers.hpp"
#include "actions/DocumentTabs.hpp"
#include "actions/audio/RevisionEdit.hpp"
#include "actions/markers/EditCommands.hpp"

#include <chrono>
#include <thread>

using namespace cupuacu;

namespace
{
    struct TitleFixture
    {
        test::StateWithTestPaths state{std::string_view{"document-title"}};

        TitleFixture()
        {
            test::ensureSdlTtfInitialized();
            std::filesystem::create_directories(state.paths->statePath());
            state.tabs.resize(2);
            for (auto &tab : state.tabs)
            {
                tab.session.document.initialize(SampleFormat::FLOAT32, 44100,
                                                1, 16);
                tab.session.bindReadRevision(storage::AudioEditRevision::silence(
                    {16, 1, 44100, SampleFormat::FLOAT32}));
                tab.session.currentFile = "original.wav";
            }
            state.tabs[1].session.currentFile = "other.wav";
            state.mainDocumentSessionWindow =
                std::make_unique<gui::DocumentSessionWindow>(
                    &state, &state.getActiveDocumentSession(),
                    &state.getActiveViewState(), "initial", 320, 180,
                    SDL_WINDOW_HIDDEN);
            REQUIRE(state.mainDocumentSessionWindow->getWindow()->getSdlWindow());
            actions::setMainWindowTitleToActiveDocument(&state);
        }

        std::string title() const
        {
            return SDL_GetWindowTitle(
                state.mainDocumentSessionWindow->getWindow()->getSdlWindow());
        }

        void edit(const int index = 0, const bool alreadyApplied = false)
        {
            auto before = actions::audio::RevisionEditState::capture(
                state.tabs[index].session);
            auto after = before;
            storage::AudioEditTransaction transaction(*before.audio);
            transaction.erase(0, 1);
            after.audio = transaction.finish();
            auto command = std::make_shared<actions::audio::RevisionEdit>(
                &state, index, "Delete", before, after);
            if (alreadyApplied)
            {
                command->redo();
                REQUIRE(command->lastOperationCommitted());
                state.addUndoableToTab(index, command);
            }
            else
            {
                state.addAndDoUndoableToTab(index, command);
            }
        }
    };
}

TEST_CASE("Native document title follows edits and the saved state",
          "[integration][document-title]")
{
    const bool markerEdit = GENERATE(false, true);
    TitleFixture fixture;
    auto &state = fixture.state;
    REQUIRE(fixture.title() == "original.wav");
    if (markerEdit)
        REQUIRE(actions::markers::insertMarkerAtCursor(&state) != 0);
    else
        fixture.edit();
    REQUIRE(fixture.title() == "original.wav*");
    state.undo();
    REQUIRE(fixture.title() == "original.wav");
    state.redo();
    REQUIRE(fixture.title() == "original.wav*");

    const auto output = state.paths->statePath() / "saved.wav";
    REQUIRE(actions::saveAs(&state, output.string()));
    REQUIRE(fixture.title() == "saved.wav");
    REQUIRE_FALSE(state.getActiveUndoables().empty());
    state.undo();
    REQUIRE(fixture.title() == "saved.wav*");
    state.redo();
    REQUIRE(fixture.title() == "saved.wav");
    state.undo();
    REQUIRE(actions::overwrite(&state));
    REQUIRE(fixture.title() == "saved.wav");
    state.redo();
    REQUIRE(fixture.title() == "saved.wav*");
}

TEST_CASE("Native title handles recorded edits and inactive documents",
          "[integration][document-title]")
{
    TitleFixture fixture;
    fixture.edit(1, true);
    REQUIRE(fixture.title() == "original.wav");
    REQUIRE(actions::switchToTab(&fixture.state, 1));
    REQUIRE(fixture.title() == "other.wav*");
    fixture.state.undo();
    REQUIRE(fixture.title() == "other.wav");
    fixture.edit(1, true);
    REQUIRE(fixture.title() == "other.wav*");
}

TEST_CASE("Native title remains clean for history without document changes",
          "[integration][document-title]")
{
    TitleFixture fixture;
    auto &state = fixture.state;
    auto before = actions::audio::RevisionEditState::capture(
        state.getActiveDocumentSession());
    state.addAndDoUndoable(std::make_shared<actions::audio::RevisionEdit>(
        &state, 0, "Copy", before, before));
    REQUIRE_FALSE(state.getActiveUndoables().empty());
    REQUIRE(fixture.title() == "original.wav");
    state.undo();
    REQUIRE(fixture.title() == "original.wav");
    state.redo();
    REQUIRE(fixture.title() == "original.wav");
}

TEST_CASE("Background save refreshes only the saved document title",
          "[integration][document-title]")
{
    const bool newerEdit = GENERATE(false, true);
    const bool switchTabs = GENERATE(false, true);
    TitleFixture fixture;
    auto &state = fixture.state;
    fixture.edit();
    const auto output = state.paths->statePath() / "background.wav";
    const auto settings = *file::defaultExportSettingsForPath(
        output, SampleFormat::FLOAT32);
    test::integration::HeldTaskScheduler held(state);
    REQUIRE(actions::io::queueSaveAs(&state, output.string(), settings));
    if (newerEdit)
        fixture.edit();
    if (switchTabs)
        REQUIRE(actions::switchToTab(&state, 1));
    held.resume();
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (state.backgroundSaveJob)
    {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        actions::io::processPendingSaveWork(&state);
        std::this_thread::yield();
    }
    REQUIRE(state.tabs[0].session.currentFile == output.string());
    if (switchTabs)
    {
        REQUIRE(fixture.title() == "other.wav");
        REQUIRE(actions::switchToTab(&state, 0));
    }
    REQUIRE(fixture.title() ==
            (newerEdit ? "background.wav*" : "background.wav"));
}
