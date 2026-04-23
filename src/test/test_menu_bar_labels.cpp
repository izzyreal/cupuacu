#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "State.hpp"
#include "TestPaths.hpp"
#include "TestSdlTtfGuard.hpp"
#include "actions/Undoable.hpp"
#include "file/AudioExport.hpp"
#include "file/SndfilePath.hpp"
#include "file/file_loading.hpp"
#include "gui/Component.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/Label.hpp"
#include "gui/Menu.hpp"
#include "gui/MenuBar.hpp"
#include "gui/MenuBarPlanning.hpp"
#include "gui/MarkerEditorDialogWindow.hpp"
#include "gui/Window.hpp"

#include <sndfile.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    class RootComponent : public cupuacu::gui::Component
    {
    public:
        explicit RootComponent(cupuacu::State *state) : Component(state, "Root")
        {
        }
    };

    class TestUndoable : public cupuacu::actions::Undoable
    {
    public:
        TestUndoable(cupuacu::State *state, std::string description)
            : Undoable(state), description(std::move(description))
        {
        }

        std::string description;

        void redo() override {}
        void undo() override {}

        std::string getRedoDescription() override
        {
            return description;
        }

        std::string getUndoDescription() override
        {
            return description;
        }

        [[nodiscard]] cupuacu::file::OverwritePreservationMutation
        overwritePreservationMutation() const override
        {
            return cupuacu::file::OverwritePreservationMutationHelper::
                compatible();
        }
    };

    std::vector<cupuacu::gui::Menu *>
    menuChildren(cupuacu::gui::Component *parent)
    {
        std::vector<cupuacu::gui::Menu *> result;
        for (const auto &child : parent->getChildren())
        {
            if (auto *menu = dynamic_cast<cupuacu::gui::Menu *>(child.get()))
            {
                result.push_back(menu);
            }
        }
        return result;
    }

    cupuacu::gui::Label *findMenuLabel(cupuacu::gui::Menu *menu)
    {
        return dynamic_cast<cupuacu::gui::Label *>(
            menu->getChildren().front().get());
    }

    cupuacu::gui::MouseEvent leftMouseDown()
    {
        return cupuacu::gui::MouseEvent{
            cupuacu::gui::DOWN,
            0,
            0,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            cupuacu::gui::MouseButtonState{true, false, false},
            1};
    }

    class ScopedDirCleanup
    {
    public:
        explicit ScopedDirCleanup(std::filesystem::path rootDir)
            : root(std::move(rootDir))
        {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
            std::filesystem::create_directories(root, ec);
        }

        ~ScopedDirCleanup()
        {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }

        const std::filesystem::path &path() const
        {
            return root;
        }

    private:
        std::filesystem::path root;
    };

    std::filesystem::path makeUniqueTempDir(const std::string &prefix)
    {
        const auto tempRoot = std::filesystem::temp_directory_path();
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dis;

        for (int attempt = 0; attempt < 32; ++attempt)
        {
            const auto now =
                std::chrono::high_resolution_clock::now().time_since_epoch();
            const auto tick = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(now)
                    .count());
            const auto candidate =
                tempRoot / (prefix + "-" + std::to_string(tick) + "-" +
                            std::to_string(dis(gen)));
            std::error_code ec;
            if (!std::filesystem::exists(candidate, ec))
            {
                return candidate;
            }
        }

        return tempRoot / (prefix + "-fallback");
    }

    void writeTestWav(const std::filesystem::path &path, const int sampleRate,
                      const int channels,
                      const std::vector<float> &interleavedFrames)
    {
        REQUIRE(channels > 0);
        REQUIRE(interleavedFrames.size() % channels == 0);

        SF_INFO info{};
        info.samplerate = sampleRate;
        info.channels = channels;
        info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

        SNDFILE *file = cupuacu::file::openSndfile(path, SFM_WRITE, &info);
        REQUIRE(file != nullptr);

        const sf_count_t frameCount =
            static_cast<sf_count_t>(interleavedFrames.size() / channels);
        const sf_count_t written =
            sf_writef_float(file, interleavedFrames.data(), frameCount);

        sf_close(file);
        REQUIRE(written == frameCount);
    }

    void writePcm16TestWav(const std::filesystem::path &path,
                           const int sampleRate, const int channels,
                           const std::vector<int16_t> &interleavedFrames)
    {
        REQUIRE(channels > 0);
        REQUIRE(interleavedFrames.size() % channels == 0);

        SF_INFO info{};
        info.samplerate = sampleRate;
        info.channels = channels;
        info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

        SNDFILE *file = cupuacu::file::openSndfile(path, SFM_WRITE, &info);
        REQUIRE(file != nullptr);

        const sf_count_t frameCount =
            static_cast<sf_count_t>(interleavedFrames.size() / channels);
        const sf_count_t written =
            sf_writef_short(file, interleavedFrames.data(), frameCount);

        sf_close(file);
        REQUIRE(written == frameCount);
    }

    std::vector<float> readFramesAsFloat(const std::filesystem::path &path)
    {
        SF_INFO info{};
        SNDFILE *file = cupuacu::file::openSndfile(path, SFM_READ, &info);
        REQUIRE(file != nullptr);

        std::vector<float> frames(
            static_cast<size_t>(info.frames * info.channels));
        const sf_count_t readCount =
            sf_readf_float(file, frames.data(), info.frames);
        sf_close(file);
        REQUIRE(readCount == info.frames);
        return frames;
    }

    cupuacu::gui::MenuBar *makeMenuBar(cupuacu::State *state,
                                       RootComponent &root)
    {
        auto *menuBar = root.emplaceChild<cupuacu::gui::MenuBar>(state);
        root.setBounds(0, 0, 480, 40);
        menuBar->setBounds(0, 0, 480, 40);
        return menuBar;
    }
} // namespace

TEST_CASE("MenuBar planning builds dynamic undo redo labels and availability",
          "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};

#ifdef __APPLE__
    REQUIRE(cupuacu::gui::buildUndoMenuLabel(&state) == "Undo (Cmd + Z)");
    REQUIRE(cupuacu::gui::buildRedoMenuLabel(&state) ==
            "Redo (Cmd + Shift + Z)");
#else
    REQUIRE(cupuacu::gui::buildUndoMenuLabel(&state) == "Undo (Ctrl + Z)");
    REQUIRE(cupuacu::gui::buildRedoMenuLabel(&state) ==
            "Redo (Ctrl + Shift + Z)");
#endif

    REQUIRE_FALSE(cupuacu::gui::isSelectionEditAvailable(&state));
    REQUIRE_FALSE(cupuacu::gui::isPasteAvailable(&state));

    state.getActiveDocumentSession().document.initialize(
        cupuacu::SampleFormat::FLOAT32, 44100, 1, 4);
    state.getActiveDocumentSession().selection.setHighest(4.0);
    state.getActiveDocumentSession().selection.setValue1(1.0);
    state.getActiveDocumentSession().selection.setValue2(3.0);
    state.clipboard.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, 2);

    REQUIRE(cupuacu::gui::isSelectionEditAvailable(&state));
    REQUIRE(cupuacu::gui::isPasteAvailable(&state));

    auto undoable = std::make_shared<TestUndoable>(&state, "Sample edit");
    state.addUndoable(undoable);

#ifdef __APPLE__
    REQUIRE(cupuacu::gui::buildUndoMenuLabel(&state) ==
            "Undo Sample edit (Cmd + Z)");
#else
    REQUIRE(cupuacu::gui::buildUndoMenuLabel(&state) ==
            "Undo Sample edit (Ctrl + Z)");
#endif

    state.undo();

#ifdef __APPLE__
    REQUIRE(cupuacu::gui::buildRedoMenuLabel(&state) ==
            "Redo Sample edit (Cmd + Shift + Z)");
#else
    REQUIRE(cupuacu::gui::buildRedoMenuLabel(&state) ==
            "Redo Sample edit (Ctrl + Shift + Z)");
#endif
}

TEST_CASE("MenuBar planning builds overwrite labels", "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};

#ifdef __APPLE__
    REQUIRE(cupuacu::gui::buildOverwriteMenuLabel(&state) ==
            "Overwrite (Cmd + S)");
#else
    REQUIRE(cupuacu::gui::buildOverwriteMenuLabel(&state) ==
            "Overwrite (Ctrl + S)");
#endif
    REQUIRE(cupuacu::gui::buildPreservingOverwriteMenuLabel() ==
            "Preserving overwrite");

    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-menu-overwrite"));
    const auto wavPath = cleanup.path() / "menu_overwrite.wav";
    writePcm16TestWav(wavPath, 44100, 1, {100, 200, 300, 400});

    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);
}

TEST_CASE("MenuBar runtime tracks open menus and hover-open state", "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    RootComponent root(&state);
    auto *menuBar = makeMenuBar(&state, root);

    auto topLevelMenus = menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 6);

    REQUIRE(menuBar->getOpenMenu() == nullptr);
    REQUIRE_FALSE(menuBar->hasMenuOpen());

    menuBar->setOpenSubMenuOnMouseOver(true);
    REQUIRE(menuBar->shouldOpenSubMenuOnMouseOver());
    REQUIRE(menuBar->mouseDown(leftMouseDown()));
    REQUIRE_FALSE(menuBar->shouldOpenSubMenuOnMouseOver());

    menuBar->hideSubMenus();
    REQUIRE(menuBar->getOpenMenu() == nullptr);
    REQUIRE_FALSE(menuBar->hasMenuOpen());
}

TEST_CASE("MenuBar disables document-dependent menus when no file is open",
          "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    RootComponent root(&state);
    auto *menuBar = makeMenuBar(&state, root);

    auto topLevelMenus = menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 6);
    auto *fileMenu = topLevelMenus[0];
    auto *generateMenu = topLevelMenus[3];
    auto *effectsMenu = topLevelMenus[4];

    auto generateEntries = menuChildren(generateMenu);
    REQUIRE(generateEntries.size() == 1);
    REQUIRE(generateEntries[0]->mouseDown(leftMouseDown()));
    REQUIRE(state.generateSilenceDialogWindow == nullptr);

    auto effectEntries = menuChildren(effectsMenu);
    REQUIRE(effectEntries.size() == 5);
    REQUIRE(effectEntries[0]->mouseDown(leftMouseDown()));
    REQUIRE(effectEntries[1]->mouseDown(leftMouseDown()));
    REQUIRE(effectEntries[2]->mouseDown(leftMouseDown()));
    REQUIRE(effectEntries[3]->mouseDown(leftMouseDown()));
    REQUIRE(effectEntries[4]->mouseDown(leftMouseDown()));
    REQUIRE(state.amplifyFadeDialog == nullptr);
    REQUIRE(state.amplifyEnvelopeDialog == nullptr);
    REQUIRE(state.dynamicsDialog == nullptr);
    REQUIRE(state.removeSilenceDialog == nullptr);

    auto fileEntries = menuChildren(fileMenu);
    REQUIRE(fileEntries.size() == 9);
    auto *saveAsEntry = fileEntries[2];
    auto *preservingSaveAsEntry = fileEntries[3];
    auto *closeEntry = fileEntries[5];
    auto *overwriteEntry = fileEntries[6];
    auto *preservingOverwriteEntry = fileEntries[7];

    REQUIRE(saveAsEntry->mouseDown(leftMouseDown()));
    REQUIRE(preservingSaveAsEntry->mouseDown(leftMouseDown()));
    REQUIRE(closeEntry->mouseDown(leftMouseDown()));
    REQUIRE(overwriteEntry->mouseDown(leftMouseDown()));
    REQUIRE(preservingOverwriteEntry->mouseDown(leftMouseDown()));
    REQUIRE(state.getActiveDocumentSession().document.getChannelCount() == 0);
    REQUIRE(state.getActiveDocumentSession().currentFile.empty());
}

TEST_CASE(
    "MenuBar file menu shows platform-aware new open close and overwrite "
    "shortcuts",
    "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    RootComponent root(&state);
    auto *menuBar = makeMenuBar(&state, root);

    auto topLevelMenus = menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 6);
    auto *fileMenu = topLevelMenus[0];
    auto fileEntries = menuChildren(fileMenu);
    REQUIRE(fileEntries.size() == 9);

#ifdef __APPLE__
    REQUIRE(fileEntries[0]->getMenuName() == "New file (Cmd + N)");
    REQUIRE(fileEntries[1]->getMenuName() == "Open (Cmd + O)");
    REQUIRE(fileEntries[2]->getMenuName() == "Save as (Cmd + Shift + S)");
    REQUIRE(fileEntries[3]->getMenuName() == "Preserving save as...");
    REQUIRE(fileEntries[5]->getMenuName() == "Close file (Cmd + W)");
    REQUIRE(fileEntries[6]->getMenuName() == "Overwrite (Cmd + S)");
    REQUIRE(fileEntries[7]->getMenuName() == "Preserving overwrite");
#else
    REQUIRE(fileEntries[0]->getMenuName() == "New file (Ctrl + N)");
    REQUIRE(fileEntries[1]->getMenuName() == "Open (Ctrl + O)");
    REQUIRE(fileEntries[2]->getMenuName() == "Save as (Ctrl + Shift + S)");
    REQUIRE(fileEntries[3]->getMenuName() == "Preserving save as...");
    REQUIRE(fileEntries[5]->getMenuName() == "Close file (Ctrl + W)");
    REQUIRE(fileEntries[6]->getMenuName() == "Overwrite (Ctrl + S)");
    REQUIRE(fileEntries[7]->getMenuName() == "Preserving overwrite");
#endif
}

TEST_CASE(
    "MenuBar file save entries expose action tooltips and disabled reasons",
    "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    RootComponent root(&state);
    auto *menuBar = makeMenuBar(&state, root);

    auto *fileMenu = menuChildren(menuBar)[0];
    auto fileEntries = menuChildren(fileMenu);
    REQUIRE(fileEntries.size() == 9);

    auto *saveAsEntry = fileEntries[2];
    auto *preservingSaveAsEntry = fileEntries[3];
    auto *overwriteEntry = fileEntries[6];
    auto *preservingOverwriteEntry = fileEntries[7];

    REQUIRE(
        saveAsEntry->getTooltipText() ==
        "Write the active document to a new file and make that file the "
        "current document path.\n\nCurrently unavailable: No document is open");
    REQUIRE(preservingSaveAsEntry->getTooltipText() ==
            "Write a new file in preservation mode, keeping unchanged source "
            "audio bytes intact where possible for supported preserving "
            "formats and using the latest opened or saved file as the "
            "reference.\n\nCurrently unavailable: No document is open");
    REQUIRE(overwriteEntry->getTooltipText() ==
            "Rewrite the current file at its existing path using the current "
            "export settings.\n\nCurrently unavailable: No document is open");
    REQUIRE(preservingOverwriteEntry->getTooltipText() ==
            "Rewrite the current file in preservation mode, keeping unchanged "
            "source audio bytes intact where possible for supported "
            "preserving formats.\n\nCurrently unavailable: No document is "
            "open");

    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-menu-tooltips"));
    const auto wavPath = cleanup.path() / "tooltip.wav";
    writePcm16TestWav(wavPath, 44100, 1, {100, 200, 300, 400});

    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);

    REQUIRE(saveAsEntry->getTooltipText() ==
            "Write the active document to a new file and make that file the "
            "current document path.");
    REQUIRE(overwriteEntry->getTooltipText() ==
            "Rewrite the current file at its existing path using the current "
            "export settings.");
    REQUIRE(preservingSaveAsEntry->getTooltipText() ==
            "Write a new file in preservation mode, keeping unchanged source "
            "audio bytes intact where possible for supported preserving "
            "formats and using the latest opened or saved file as the "
            "reference.");
    REQUIRE(preservingOverwriteEntry->getTooltipText() ==
            "Rewrite the current file in preservation mode, keeping unchanged "
            "source audio bytes intact where possible for supported "
            "preserving formats.");

    state.getActiveDocumentSession().document.addMarker(1, "Kick");

    REQUIRE(saveAsEntry->getTooltipText() ==
            "Write the active document to a new file and make that file the "
            "current document path.\n\nMarkers: outcome depends on the chosen "
            "export format.");
    REQUIRE(preservingSaveAsEntry->getTooltipText() ==
            "Write a new file in preservation mode, keeping unchanged source "
            "audio bytes intact where possible for supported preserving "
            "formats and using the latest opened or saved file as the "
            "reference.\n\nMarkers: outcome depends on the chosen export "
            "format.");
    REQUIRE(overwriteEntry->getTooltipText() ==
            "Rewrite the current file at its existing path using the current "
            "export settings.\n\nMarkers: native support, exact round-trip.");
    REQUIRE(preservingOverwriteEntry->getTooltipText() ==
            "Rewrite the current file in preservation mode, keeping unchanged "
            "source audio bytes intact where possible for supported "
            "preserving formats.\n\nMarkers: native support, exact "
            "round-trip.");
}

TEST_CASE(
    "MenuBar edit actions invoke trim copy cut and paste through submenu "
    "actions",
    "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    RootComponent root(&state);
    auto *menuBar = makeMenuBar(&state, root);
    auto *editMenu = menuChildren(menuBar)[1];
    auto editEntries = menuChildren(editMenu);
    REQUIRE(editEntries.size() == 9);

    auto &session = state.getActiveDocumentSession();
    auto &doc = session.document;
    doc.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, 5);
    for (int i = 0; i < 5; ++i)
    {
        doc.setSample(0, i, 0.1f * static_cast<float>(i + 1), false);
    }
    doc.updateWaveformCache();

    session.selection.setHighest(5.0);
    session.selection.setValue1(1.0);
    session.selection.setValue2(4.0);

    auto *trimEntry = editEntries[2];
    trimEntry->mouseDown(leftMouseDown());
    REQUIRE(doc.getFrameCount() == 3);
    REQUIRE(session.selection.isActive());
    REQUIRE(session.selection.getStartInt() == 0);
    REQUIRE(session.selection.getLengthInt() == 3);
    REQUIRE(doc.getSample(0, 0) == Catch::Approx(0.2f));
    REQUIRE(doc.getSample(0, 2) == Catch::Approx(0.4f));

    state.undo();
    REQUIRE(doc.getFrameCount() == 5);
    REQUIRE(session.selection.getStartInt() == 1);
    REQUIRE(session.selection.getLengthInt() == 3);

    auto *copyEntry = editEntries[4];
    copyEntry->mouseDown(leftMouseDown());
    REQUIRE(state.clipboard.getFrameCount() == 3);
    REQUIRE(state.clipboard.getSample(0, 0) == Catch::Approx(0.2f));
    REQUIRE(state.clipboard.getSample(0, 2) == Catch::Approx(0.4f));

    auto *cutEntry = editEntries[3];
    cutEntry->mouseDown(leftMouseDown());
    REQUIRE(doc.getFrameCount() == 2);
    REQUIRE_FALSE(session.selection.isActive());
    REQUIRE(session.cursor == 1);
    REQUIRE(doc.getSample(0, 0) == Catch::Approx(0.1f));
    REQUIRE(doc.getSample(0, 1) == Catch::Approx(0.5f));

    session.selection.reset();
    session.cursor = 1;
    auto *pasteEntry = editEntries[5];
    pasteEntry->mouseDown(leftMouseDown());
    REQUIRE(doc.getFrameCount() == 5);
    REQUIRE(session.selection.isActive());
    REQUIRE(session.selection.getStartInt() == 1);
    REQUIRE(session.selection.getLengthInt() == 3);
    REQUIRE(doc.getSample(0, 1) == Catch::Approx(0.2f));
    REQUIRE(doc.getSample(0, 3) == Catch::Approx(0.4f));

    session.selection.reset();
    session.cursor = 2;
    auto *insertMarkerEntry = editEntries[6];
    insertMarkerEntry->mouseDown(leftMouseDown());
    REQUIRE(doc.getMarkers().size() == 1);
    REQUIRE(doc.getMarkers()[0].frame == 2);
    REQUIRE(state.getActiveViewState().selectedMarkerId == doc.getMarkers()[0].id);

    auto *editMarkersEntry = editEntries[7];
    editMarkersEntry->mouseDown(leftMouseDown());
    REQUIRE(state.markerEditorDialogWindow != nullptr);
    REQUIRE(state.markerEditorDialogWindow->getMarkerId() ==
            doc.getMarkers()[0].id);
}

TEST_CASE("MenuBar view menu toggles snap and persists it", "[gui][persistence]")
{
    const auto rootPath =
        cupuacu::test::makeUniqueTestRoot("menu-view-snap-persist");
    cupuacu::test::StateWithTestPaths state{rootPath};
    RootComponent root(&state);
    auto *menuBar = makeMenuBar(&state, root);
    auto *viewMenu = menuChildren(menuBar)[2];
    auto viewEntries = menuChildren(viewMenu);
    REQUIRE(viewEntries.size() == 6);
    REQUIRE(viewEntries[5]->getMenuName() == "[ ] Snap");
    REQUIRE_FALSE(state.snapEnabled);

    viewEntries[5]->mouseDown(leftMouseDown());
    REQUIRE(state.snapEnabled);
    REQUIRE(viewEntries[5]->getMenuName() == "[x] Snap");

    const auto loadedSession =
        cupuacu::persistence::SessionStatePersistence::load(
            state.paths->sessionStatePath());
    REQUIRE(loadedSession.snapEnabled);
}

TEST_CASE("MenuBar split by markers creates one new document per marker gap",
          "[gui]")
{
    cupuacu::test::StateWithTestPaths state{};
    RootComponent root(&state);
    auto *menuBar = makeMenuBar(&state, root);
    auto *editMenu = menuChildren(menuBar)[1];
    auto editEntries = menuChildren(editMenu);
    REQUIRE(editEntries.size() == 9);

    auto &session = state.getActiveDocumentSession();
    auto &doc = session.document;
    doc.initialize(cupuacu::SampleFormat::FLOAT32, 44100, 1, 12);
    for (int i = 0; i < 12; ++i)
    {
        doc.setSample(0, i, static_cast<float>(i + 1), false);
    }
    doc.addMarker(2, "A");
    doc.addMarker(5, "B");
    doc.addMarker(9, "C");

    auto *splitByMarkersEntry = editEntries[8];
    splitByMarkersEntry->mouseDown(leftMouseDown());

    REQUIRE(state.tabs.size() == 3);
    REQUIRE(state.tabs[1].session.document.getFrameCount() == 3);
    REQUIRE(state.tabs[2].session.document.getFrameCount() == 4);
    REQUIRE(state.tabs[1].session.document.getSample(0, 0) == Catch::Approx(3.0f));
    REQUIRE(state.tabs[1].session.document.getSample(0, 2) == Catch::Approx(5.0f));
    REQUIRE(state.tabs[2].session.document.getSample(0, 0) == Catch::Approx(6.0f));
    REQUIRE(state.tabs[2].session.document.getSample(0, 3) == Catch::Approx(9.0f));
    REQUIRE(state.tabs[1].session.document.getMarkers().size() == 2);
    REQUIRE(state.tabs[2].session.document.getMarkers().size() == 2);
}

TEST_CASE("MenuBar file overwrite action rewrites the current file",
          "[gui][file]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-menu-overwrite"));
    const auto wavPath = cleanup.path() / "menu_overwrite.wav";
    writePcm16TestWav(wavPath, 32000, 1, {0, 0, 0});

    cupuacu::test::StateWithTestPaths state{};
    state.getActiveDocumentSession().currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);
    state.getActiveDocumentSession().document.setSample(0, 0, 0.25f, false);
    state.getActiveDocumentSession().document.setSample(0, 1, -0.5f, false);
    state.getActiveDocumentSession().document.setSample(0, 2, 0.75f, false);

    RootComponent root(&state);
    auto *menuBar = makeMenuBar(&state, root);
    auto *fileMenu = menuChildren(menuBar)[0];
    auto fileEntries = menuChildren(fileMenu);
    REQUIRE(fileEntries.size() == 9);

    auto *overwriteEntry = fileEntries[6];
    overwriteEntry->mouseDown(leftMouseDown());

    const auto frames = readFramesAsFloat(wavPath);
    REQUIRE(frames.size() == 3);
    REQUIRE(frames[0] == Catch::Approx(0.25f).margin(1e-4));
    REQUIRE(frames[1] == Catch::Approx(-0.5f).margin(1e-4));
    REQUIRE(frames[2] == Catch::Approx(0.75f).margin(1e-4));
}
