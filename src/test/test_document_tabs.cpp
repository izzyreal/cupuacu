#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "TestPaths.hpp"
#include "actions/DocumentLifecycle.hpp"
#include "actions/DocumentTabs.hpp"
#include "actions/io/BackgroundSave.hpp"
#include "file/AudioExport.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/TabStrip.hpp"

#include <chrono>
#include <filesystem>
#include <thread>

namespace
{
    void drainPendingSaveWork(cupuacu::State *state)
    {
        for (int attempt = 0; attempt < 5000; ++attempt)
        {
            cupuacu::actions::io::processPendingSaveWork(state);
            if (!state->backgroundSaveJob)
            {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        FAIL("Timed out waiting for background save work");
    }
} // namespace

TEST_CASE("Document tabs can switch without an audio backend", "[tabs]")
{
    cupuacu::test::StateWithTestPaths state{};

    REQUIRE(cupuacu::actions::canSwitchTabs(&state));
}

TEST_CASE("Document tab title uses the file name when present", "[tabs]")
{
    cupuacu::DocumentTab tab;
    tab.session.currentFile = "/tmp/projects/example.wav";

    REQUIRE(cupuacu::actions::documentTabTitle(tab) == "example.wav");

    tab.session.autosaveSnapshotPath = "/tmp/document.cupuacu-autosave";
    REQUIRE(cupuacu::actions::documentTabTitle(tab) == "example.wav*");

    tab.session.currentFile.clear();
    tab.session.autosaveSnapshotPath.clear();
    REQUIRE(cupuacu::actions::documentTabTitle(tab) ==
            cupuacu::actions::kUntitledDocumentTitle);

    tab.session.document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 16);
    REQUIRE(cupuacu::actions::documentTabTitle(tab) ==
            std::string(cupuacu::actions::kUntitledDocumentTitle) + "*");
}

TEST_CASE("Document session unsaved helper matches title logic", "[tabs]")
{
    cupuacu::DocumentSession session;

    REQUIRE_FALSE(cupuacu::actions::documentSessionHasUnsavedChanges(session));

    session.currentFile = "/tmp/projects/example.wav";
    session.autosaveSnapshotPath = "/tmp/document.cupuacu-autosave";
    REQUIRE(cupuacu::actions::documentSessionHasUnsavedChanges(session));

    session.currentFile.clear();
    session.autosaveSnapshotPath.clear();
    session.document.initialize(cupuacu::SampleFormat::PCM_S16, 44100, 1, 16);
    REQUIRE(cupuacu::actions::documentSessionHasUnsavedChanges(session));
}

TEST_CASE("Creating an empty tab appends and activates it", "[tabs]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.tabs.resize(1);
    state.activeTabIndex = 0;
    state.tabs[0].session.currentFile = "/tmp/original.wav";
    state.tabs[0].session.document.initialize(cupuacu::SampleFormat::PCM_S16,
                                              44100, 2, 64);
    state.tabs[0].undoables.push_back(nullptr);

    REQUIRE(cupuacu::actions::createEmptyTab(&state));
    REQUIRE(state.tabs.size() == 2);
    REQUIRE(state.activeTabIndex == 1);
    REQUIRE(state.tabs[0].session.currentFile == "/tmp/original.wav");
    REQUIRE(state.tabs[0].session.document.getFrameCount() == 64);
    REQUIRE(state.tabs[0].undoables.size() == 1);
    REQUIRE(state.getActiveDocumentSession().currentFile.empty());
    REQUIRE(state.getActiveDocumentSession().document.getChannelCount() == 0);
    REQUIRE(state.getActiveUndoables().empty());
    REQUIRE(state.getActiveRedoables().empty());
}

TEST_CASE("Creating a new document reuses the lone blank startup tab", "[tabs]")
{
    cupuacu::test::StateWithTestPaths state{};

    REQUIRE(cupuacu::actions::createNewDocumentInNewTab(
        &state, 44100, cupuacu::SampleFormat::PCM_S16, 2));
    REQUIRE(state.tabs.size() == 1);
    REQUIRE(state.activeTabIndex == 0);
    REQUIRE(state.getActiveDocumentSession().document.getSampleRate() == 44100);
    REQUIRE(state.getActiveDocumentSession().document.getChannelCount() == 2);
}

TEST_CASE("Creating a new document appends a tab when a document is already open",
          "[tabs]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = "/tmp/original.wav";
    state.getActiveDocumentSession().document.initialize(
        cupuacu::SampleFormat::PCM_S16, 44100, 2, 64);

    REQUIRE(cupuacu::actions::createNewDocumentInNewTab(
        &state, 48000, cupuacu::SampleFormat::PCM_S8, 1));
    REQUIRE(state.tabs.size() == 2);
    REQUIRE(state.activeTabIndex == 1);
    REQUIRE(state.tabs[0].session.currentFile == "/tmp/original.wav");
    REQUIRE(state.tabs[0].session.document.getFrameCount() == 64);
    REQUIRE(state.getActiveDocumentSession().document.getSampleRate() == 48000);
    REQUIRE(state.getActiveDocumentSession().document.getChannelCount() == 1);
}

TEST_CASE("Switching tabs changes the active document context", "[tabs]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.tabs.resize(2);
    state.tabs[0].session.currentFile = "/tmp/first.wav";
    state.tabs[1].session.currentFile = "/tmp/second.wav";
    state.activeTabIndex = 0;

    REQUIRE(cupuacu::actions::switchToTab(&state, 1));
    REQUIRE(state.activeTabIndex == 1);
    REQUIRE(state.getActiveDocumentSession().currentFile == "/tmp/second.wav");
    REQUIRE_FALSE(cupuacu::actions::switchToTab(&state, 1));
}

TEST_CASE("Tab cycling wraps forward and backward across open tabs", "[tabs]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.tabs.resize(3);
    state.tabs[0].session.currentFile = "/tmp/first.wav";
    state.tabs[1].session.currentFile = "/tmp/second.wav";
    state.tabs[2].session.currentFile = "/tmp/third.wav";
    state.activeTabIndex = 2;

    REQUIRE(cupuacu::actions::switchToNextTab(&state));
    REQUIRE(state.activeTabIndex == 0);
    REQUIRE(state.getActiveDocumentSession().currentFile == "/tmp/first.wav");

    REQUIRE(cupuacu::actions::switchToPreviousTab(&state));
    REQUIRE(state.activeTabIndex == 2);
    REQUIRE(state.getActiveDocumentSession().currentFile == "/tmp/third.wav");
}

TEST_CASE("Tab strip exposes absolute file paths as tooltip text", "[tabs]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.tabs[0].session.currentFile = "/tmp/projects/first.wav";

    cupuacu::gui::TabStrip tabStrip(&state);
    tabStrip.timerCallback();

    REQUIRE(tabStrip.getChildren().size() == 1);
    auto *tab = dynamic_cast<cupuacu::gui::TabStripTab *>(
        tabStrip.getChildren().front().get());
    REQUIRE(tab != nullptr);
    REQUIRE(tab->getTooltipText() == "/tmp/projects/first.wav");

    state.tabs[0].session.currentFile.clear();
    tabStrip.timerCallback();
    REQUIRE(tab->getTooltipText().empty());
}

TEST_CASE("Closing the active tab removes it and keeps a valid selection",
          "[tabs]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.tabs.resize(3);
    state.tabs[0].session.currentFile = "/tmp/first.wav";
    state.tabs[1].session.currentFile = "/tmp/second.wav";
    state.tabs[2].session.currentFile = "/tmp/third.wav";
    state.activeTabIndex = 1;

    REQUIRE(cupuacu::actions::closeActiveTab(&state));
    REQUIRE(state.tabs.size() == 2);
    REQUIRE(state.activeTabIndex == 1);
    REQUIRE(state.getActiveDocumentSession().currentFile == "/tmp/third.wav");
}

TEST_CASE("Closing a dirty tab can be canceled", "[tabs]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.tabs.resize(2);
    state.activeTabIndex = 0;
    state.tabs[0].session.currentFile = "/tmp/dirty.wav";
    state.tabs[0].session.autosaveSnapshotPath = "/tmp/dirty.cupuacu-autosave";
    state.tabs[0].session.document.initialize(
        cupuacu::SampleFormat::PCM_S16, 44100, 2, 64);
    state.tabs[1].session.currentFile = "/tmp/other.wav";

    state.unsavedChangesReporter = [](const std::string &, const std::string &)
    {
        return cupuacu::UnsavedChangesChoice::Cancel;
    };

    REQUIRE_FALSE(cupuacu::actions::closeActiveTab(&state));
    REQUIRE(state.tabs.size() == 2);
    REQUIRE(state.activeTabIndex == 0);
    REQUIRE(state.getActiveDocumentSession().currentFile == "/tmp/dirty.wav");
}

TEST_CASE("Closing a dirty tab can discard unsaved edits", "[tabs]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.tabs.resize(2);
    state.activeTabIndex = 0;
    state.tabs[0].session.currentFile = "/tmp/dirty.wav";
    state.tabs[0].session.autosaveSnapshotPath = "/tmp/dirty.cupuacu-autosave";
    state.tabs[0].session.document.initialize(
        cupuacu::SampleFormat::PCM_S16, 44100, 2, 64);
    state.tabs[1].session.currentFile = "/tmp/other.wav";

    state.unsavedChangesReporter = [](const std::string &, const std::string &)
    {
        return cupuacu::UnsavedChangesChoice::Discard;
    };

    REQUIRE(cupuacu::actions::closeActiveTab(&state));
    REQUIRE(state.tabs.size() == 1);
    REQUIRE(state.activeTabIndex == 0);
    REQUIRE(state.getActiveDocumentSession().currentFile == "/tmp/other.wav");
}

TEST_CASE("Closing a dirty file-backed tab can save before closing", "[tabs]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("close-tab-save");
    const auto outputPath = root / "documents" / "dirty.wav";

    cupuacu::test::StateWithTestPaths state{root};
    state.tabs.resize(2);
    state.activeTabIndex = 0;
    state.tabs[0].session.currentFile = outputPath.string();
    state.tabs[0].session.autosaveSnapshotPath = "/tmp/dirty.cupuacu-autosave";
    state.tabs[0].session.document.initialize(
        cupuacu::SampleFormat::PCM_S16, 44100, 2, 64);
    state.tabs[1].session.currentFile = "/tmp/other.wav";

    state.unsavedChangesReporter = [](const std::string &, const std::string &)
    {
        return cupuacu::UnsavedChangesChoice::Save;
    };

    REQUIRE(cupuacu::actions::closeActiveTab(&state));
    REQUIRE(state.tabs.size() == 1);
    REQUIRE(state.activeTabIndex == 0);
    REQUIRE(state.getActiveDocumentSession().currentFile == "/tmp/other.wav");
    REQUIRE(std::filesystem::exists(outputPath));
}

TEST_CASE("Pending close completes after save as finishes", "[tabs]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("close-tab-save-as");
    const auto outputPath = root / "documents" / "saved.wav";

    cupuacu::test::StateWithTestPaths state{root};
    state.tabs.resize(2);
    state.activeTabIndex = 0;
    state.tabs[0].session.document.initialize(
        cupuacu::SampleFormat::PCM_S16, 44100, 2, 64);
    state.tabs[0].session.autosaveSnapshotPath = "/tmp/dirty.cupuacu-autosave";
    state.tabs[1].session.currentFile = "/tmp/other.wav";

    auto settings = cupuacu::file::defaultExportSettingsForPath(
        outputPath, state.tabs[0].session.document.getSampleFormat());
    REQUIRE(settings.has_value());

    state.pendingCloseTabAfterSaveId = state.tabs[0].id;
    REQUIRE(cupuacu::actions::io::queueSaveAs(&state, outputPath.string(),
                                              *settings));
    REQUIRE(state.backgroundSaveJob != nullptr);

    drainPendingSaveWork(&state);

    REQUIRE(state.tabs.size() == 1);
    REQUIRE(state.activeTabIndex == 0);
    REQUIRE(state.getActiveDocumentSession().currentFile == "/tmp/other.wav");
    REQUIRE(std::filesystem::exists(outputPath));
    REQUIRE_FALSE(state.pendingCloseTabAfterSaveId.has_value());
}

TEST_CASE("Closing an inactive tab preserves the active document context",
          "[tabs]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.tabs.resize(3);
    state.tabs[0].session.currentFile = "/tmp/first.wav";
    state.tabs[1].session.currentFile = "/tmp/second.wav";
    state.tabs[2].session.currentFile = "/tmp/third.wav";
    state.activeTabIndex = 2;

    REQUIRE(cupuacu::actions::closeTab(&state, 0));
    REQUIRE(state.tabs.size() == 2);
    REQUIRE(state.activeTabIndex == 1);
    REQUIRE(state.getActiveDocumentSession().currentFile == "/tmp/third.wav");
}

TEST_CASE("Closing the only tab clears its document but keeps the tab", "[tabs]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.tabs.resize(1);
    state.activeTabIndex = 0;
    state.getActiveDocumentSession().currentFile = "/tmp/only.wav";
    state.getActiveDocumentSession().document.initialize(
        cupuacu::SampleFormat::PCM_S16, 48000, 2, 128);
    state.getActiveUndoables().push_back(nullptr);

    REQUIRE(cupuacu::actions::closeActiveTab(&state));
    REQUIRE(state.tabs.size() == 1);
    REQUIRE(state.activeTabIndex == 0);
    REQUIRE(state.getActiveDocumentSession().currentFile.empty());
    REQUIRE(state.getActiveDocumentSession().document.getChannelCount() == 0);
    REQUIRE(state.getActiveUndoables().empty());
}

TEST_CASE("Active tab body clicks are consumed without reselecting the tab",
          "[tabs]")
{
    cupuacu::test::StateWithTestPaths state{};
    cupuacu::gui::TabStripTab tab(&state, "Active", 0);
    tab.setBounds(0, 0, 120, 32);
    tab.setActive(true);

    int selectCount = 0;
    int closeCount = 0;
    tab.setOnSelect([&](const int) { ++selectCount; });
    tab.setOnClose([&](const int) { ++closeCount; });

    REQUIRE(tab.mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        10,
        16,
        10.0f,
        16.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));
    REQUIRE_FALSE(tab.mouseUp(cupuacu::gui::MouseEvent{
        cupuacu::gui::UP,
        10,
        16,
        10.0f,
        16.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{false, false, false},
        1}));
    REQUIRE(selectCount == 0);
    REQUIRE(closeCount == 0);
}

TEST_CASE("Active tab close clicks still trigger close", "[tabs]")
{
    cupuacu::test::StateWithTestPaths state{};
    cupuacu::gui::TabStripTab tab(&state, "Active", 0);
    tab.setBounds(0, 0, 120, 32);
    tab.setActive(true);

    int closeIndex = -1;
    tab.setOnClose([&](const int index) { closeIndex = index; });

    REQUIRE(tab.mouseDown(cupuacu::gui::MouseEvent{
        cupuacu::gui::DOWN,
        110,
        16,
        110.0f,
        16.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{true, false, false},
        1}));
    REQUIRE(tab.mouseUp(cupuacu::gui::MouseEvent{
        cupuacu::gui::UP,
        110,
        16,
        110.0f,
        16.0f,
        0.0f,
        0.0f,
        cupuacu::gui::MouseButtonState{false, false, false},
        1}));
    REQUIRE(closeIndex == 0);
}
