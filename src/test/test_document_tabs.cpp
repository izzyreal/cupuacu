#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "actions/DocumentLifecycle.hpp"
#include "actions/DocumentTabs.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/TabStrip.hpp"

TEST_CASE("Document tabs can switch without an audio backend", "[tabs]")
{
    cupuacu::State state{};

    REQUIRE(cupuacu::actions::canSwitchTabs(&state));
}

TEST_CASE("Document tab title uses the file name when present", "[tabs]")
{
    cupuacu::DocumentTab tab;
    tab.session.currentFile = "/tmp/projects/example.wav";

    REQUIRE(cupuacu::actions::documentTabTitle(tab) == "example.wav");

    tab.session.currentFile.clear();
    REQUIRE(cupuacu::actions::documentTabTitle(tab) ==
            cupuacu::actions::kUntitledDocumentTitle);
}

TEST_CASE("Creating an empty tab appends and activates it", "[tabs]")
{
    cupuacu::State state{};
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
    cupuacu::State state{};

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
    cupuacu::State state{};
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
    cupuacu::State state{};
    state.tabs.resize(2);
    state.tabs[0].session.currentFile = "/tmp/first.wav";
    state.tabs[1].session.currentFile = "/tmp/second.wav";
    state.activeTabIndex = 0;

    REQUIRE(cupuacu::actions::switchToTab(&state, 1));
    REQUIRE(state.activeTabIndex == 1);
    REQUIRE(state.getActiveDocumentSession().currentFile == "/tmp/second.wav");
    REQUIRE_FALSE(cupuacu::actions::switchToTab(&state, 1));
}

TEST_CASE("Tab strip exposes absolute file paths as tooltip text", "[tabs]")
{
    cupuacu::State state{};
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
    cupuacu::State state{};
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

TEST_CASE("Closing an inactive tab preserves the active document context",
          "[tabs]")
{
    cupuacu::State state{};
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
    cupuacu::State state{};
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
    cupuacu::State state{};
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
    cupuacu::State state{};
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
