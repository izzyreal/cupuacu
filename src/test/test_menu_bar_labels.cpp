#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "State.hpp"
#include "TestSdlTtfGuard.hpp"
#include "actions/Undoable.hpp"
#include "file/file_loading.hpp"
#include "gui/Component.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/Label.hpp"
#include "gui/Menu.hpp"
#include "gui/MenuBar.hpp"
#include "gui/MenuBarPlanning.hpp"
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
        explicit RootComponent(cupuacu::State *state)
            : Component(state, "Root")
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
    };

    std::vector<cupuacu::gui::Menu *> menuChildren(cupuacu::gui::Component *parent)
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
        return dynamic_cast<cupuacu::gui::Label *>(menu->getChildren().front().get());
    }

    cupuacu::gui::MouseEvent leftMouseDown()
    {
        return cupuacu::gui::MouseEvent{
            cupuacu::gui::DOWN, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f,
            cupuacu::gui::MouseButtonState{true, false, false}, 1};
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
            const auto tick =
                static_cast<uint64_t>(std::chrono::duration_cast<
                                          std::chrono::nanoseconds>(now)
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

        SNDFILE *file = sf_open(path.string().c_str(), SFM_WRITE, &info);
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

        SNDFILE *file = sf_open(path.string().c_str(), SFM_WRITE, &info);
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
        SNDFILE *file = sf_open(path.string().c_str(), SFM_READ, &info);
        REQUIRE(file != nullptr);

        std::vector<float> frames(static_cast<size_t>(info.frames * info.channels));
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
    cupuacu::State state{};

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

    state.activeDocumentSession.document.initialize(cupuacu::SampleFormat::FLOAT32,
                                                    44100, 1, 4);
    state.activeDocumentSession.selection.setHighest(4.0);
    state.activeDocumentSession.selection.setValue1(1.0);
    state.activeDocumentSession.selection.setValue2(3.0);
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

TEST_CASE("MenuBar runtime tracks open menus and hover-open state", "[gui]")
{
    cupuacu::State state{};
    RootComponent root(&state);
    auto *menuBar = makeMenuBar(&state, root);

    auto topLevelMenus = menuChildren(menuBar);
    REQUIRE(topLevelMenus.size() == 6);
    auto *fileMenu = topLevelMenus[0];

    REQUIRE(menuBar->getOpenMenu() == nullptr);
    REQUIRE_FALSE(menuBar->hasMenuOpen());

    fileMenu->mouseDown(leftMouseDown());
    REQUIRE(menuBar->getOpenMenu() == fileMenu);
    REQUIRE(menuBar->hasMenuOpen());

    menuBar->setOpenSubMenuOnMouseOver(true);
    REQUIRE(menuBar->shouldOpenSubMenuOnMouseOver());
    REQUIRE(menuBar->mouseDown(leftMouseDown()));
    REQUIRE_FALSE(menuBar->shouldOpenSubMenuOnMouseOver());

    menuBar->hideSubMenus();
    REQUIRE(menuBar->getOpenMenu() == nullptr);
    REQUIRE_FALSE(menuBar->hasMenuOpen());
}

TEST_CASE("MenuBar edit actions invoke trim copy cut and paste through submenu actions",
          "[gui]")
{
    cupuacu::State state{};
    RootComponent root(&state);
    auto *menuBar = makeMenuBar(&state, root);
    auto *editMenu = menuChildren(menuBar)[1];
    auto editEntries = menuChildren(editMenu);
    REQUIRE(editEntries.size() == 6);

    auto &session = state.activeDocumentSession;
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
}

TEST_CASE("MenuBar file overwrite action rewrites the current file", "[gui][file]")
{
    ScopedDirCleanup cleanup(makeUniqueTempDir("cupuacu-test-menu-overwrite"));
    const auto wavPath = cleanup.path() / "menu_overwrite.wav";
    writePcm16TestWav(wavPath, 32000, 1, {0, 0, 0});

    cupuacu::State state{};
    state.activeDocumentSession.currentFile = wavPath.string();
    cupuacu::file::loadSampleData(&state);
    state.activeDocumentSession.document.setSample(0, 0, 0.25f, false);
    state.activeDocumentSession.document.setSample(0, 1, -0.5f, false);
    state.activeDocumentSession.document.setSample(0, 2, 0.75f, false);

    RootComponent root(&state);
    auto *menuBar = makeMenuBar(&state, root);
    auto *fileMenu = menuChildren(menuBar)[0];
    auto fileEntries = menuChildren(fileMenu);
    REQUIRE(fileEntries.size() == 6);

    auto *overwriteEntry = fileEntries[4];
    overwriteEntry->mouseDown(leftMouseDown());

    const auto frames = readFramesAsFloat(wavPath);
    REQUIRE(frames.size() == 3);
    REQUIRE(frames[0] == Catch::Approx(0.25f).margin(1e-4));
    REQUIRE(frames[1] == Catch::Approx(-0.5f).margin(1e-4));
    REQUIRE(frames[2] == Catch::Approx(0.75f).margin(1e-4));
}
