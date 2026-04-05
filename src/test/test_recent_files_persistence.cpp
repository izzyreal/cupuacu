#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestSdlTtfGuard.hpp"
#include "TestPaths.hpp"
#include "actions/DocumentLifecycle.hpp"
#include "actions/Zoom.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/DocumentSessionWindow.hpp"
#include "gui/Gui.hpp"
#include "gui/keyboard_handling.hpp"
#include "persistence/RecentFilesPersistence.hpp"
#include "persistence/SessionStatePersistence.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace
{
    class ScopedCleanup
    {
    public:
        explicit ScopedCleanup(std::filesystem::path pathToUse)
            : path(std::move(pathToUse))
        {
            std::error_code ec;
            std::filesystem::remove_all(path.parent_path(), ec);
        }

        ~ScopedCleanup()
        {
            std::error_code ec;
            std::filesystem::remove_all(path.parent_path(), ec);
        }

    private:
        std::filesystem::path path;
    };
} // namespace

TEST_CASE("Recent files persistence round-trips and trims to ten entries",
          "[persistence]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("recent-files-test");
    const auto path = root / "recently_opened_files.json";
    ScopedCleanup cleanup(path);

    std::vector<std::string> files;
    for (int i = 0; i < 12; ++i)
    {
        files.push_back("/tmp/file-" + std::to_string(i) + ".wav");
    }

    REQUIRE(cupuacu::persistence::RecentFilesPersistence::save(path, files));

    const auto loaded = cupuacu::persistence::RecentFilesPersistence::load(path);
    REQUIRE(loaded.size() == 10);
    REQUIRE(loaded.front() == "/tmp/file-0.wav");
    REQUIRE(loaded.back() == "/tmp/file-9.wav");
}

TEST_CASE("Recent files persistence rejects malformed payloads", "[persistence]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("recent-files-invalid");
    const auto path = root / "recently_opened_files.json";
    ScopedCleanup cleanup(path);

    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream out(path);
        REQUIRE(out.good());
        out << R"({"version":1,"files":["a.wav"]})";
    }

    REQUIRE(cupuacu::persistence::RecentFilesPersistence::load(path).empty());

    {
        std::ofstream out(path);
        REQUIRE(out.good());
        out << R"({"version":1,"recentFiles":[1,2,3]})";
    }

    REQUIRE(cupuacu::persistence::RecentFilesPersistence::load(path).empty());
}

TEST_CASE("Recent files persistence loads the current format",
          "[persistence]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("recent-files-current");
    const auto path = root / "recently_opened_files.json";
    ScopedCleanup cleanup(path);

    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream out(path);
        REQUIRE(out.good());
        out << R"({"version":1,"recentFiles":["/tmp/first.wav","/tmp/second.wav"]})";
    }

    const auto loaded = cupuacu::persistence::RecentFilesPersistence::load(path);
    REQUIRE(loaded ==
            std::vector<std::string>{"/tmp/first.wav", "/tmp/second.wav"});
}

TEST_CASE("Session state persistence round-trips open files and active index",
          "[persistence]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("session-state-test");
    const auto path = root / "session_state.json";
    ScopedCleanup cleanup(path);

    cupuacu::persistence::PersistedSessionState state{};
    state.openDocuments = {
        {
            .filePath = "/tmp/open-a.wav",
            .samplesPerPixel = 2.5,
            .sampleOffset = 17,
            .selectionStart = 3,
            .selectionEndExclusive = 11,
        },
        {
            .filePath = "/tmp/open-b.wav",
            .samplesPerPixel = 4.0,
            .sampleOffset = 29,
        },
    };
    state.openFiles = {"/tmp/open-a.wav", "/tmp/open-b.wav"};
    state.activeOpenFileIndex = 1;

    REQUIRE(cupuacu::persistence::SessionStatePersistence::save(path, state));

    const auto loaded = cupuacu::persistence::SessionStatePersistence::load(path);
    REQUIRE(loaded.openDocuments.size() == 2);
    REQUIRE(loaded.openDocuments[0].filePath == "/tmp/open-a.wav");
    REQUIRE(loaded.openDocuments[0].samplesPerPixel == Catch::Approx(2.5));
    REQUIRE(loaded.openDocuments[0].sampleOffset == 17);
    REQUIRE(loaded.openDocuments[0].selectionStart == 3);
    REQUIRE(loaded.openDocuments[0].selectionEndExclusive == 11);
    REQUIRE(loaded.openDocuments[1].filePath == "/tmp/open-b.wav");
    REQUIRE(loaded.openDocuments[1].samplesPerPixel == Catch::Approx(4.0));
    REQUIRE(loaded.openDocuments[1].sampleOffset == 29);
    REQUIRE(loaded.openFiles ==
            std::vector<std::string>{"/tmp/open-a.wav", "/tmp/open-b.wav"});
    REQUIRE(loaded.activeOpenFileIndex == 1);
}

TEST_CASE("Session state persistence rejects malformed payloads", "[persistence]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("session-state-invalid");
    const auto path = root / "session_state.json";
    ScopedCleanup cleanup(path);

    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream out(path);
        REQUIRE(out.good());
        out << R"({"version":2,"openDocuments":[{"filePath":123}],"activeOpenFileIndex":0})";
    }

    REQUIRE(cupuacu::persistence::SessionStatePersistence::load(path)
                .openFiles.empty());
}

TEST_CASE("Session state persistence loads legacy version 1 open files",
          "[persistence]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("session-state-legacy");
    const auto path = root / "session_state.json";
    ScopedCleanup cleanup(path);

    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream out(path);
        REQUIRE(out.good());
        out << R"({"version":1,"openFiles":["/tmp/legacy-a.wav","/tmp/legacy-b.wav"],"activeOpenFileIndex":1})";
    }

    const auto loaded = cupuacu::persistence::SessionStatePersistence::load(path);
    REQUIRE(loaded.openFiles ==
            std::vector<std::string>{"/tmp/legacy-a.wav",
                                     "/tmp/legacy-b.wav"});
    REQUIRE(loaded.openDocuments.size() == 2);
    REQUIRE(loaded.openDocuments[0].filePath == "/tmp/legacy-a.wav");
    REQUIRE_FALSE(loaded.openDocuments[0].samplesPerPixel.has_value());
    REQUIRE(loaded.activeOpenFileIndex == 1);
}

TEST_CASE("Startup document restore plan restores multiple open files and active tab",
          "[persistence]")
{
    const auto root =
        cupuacu::test::makeUniqueTestRoot("startup-restore-multi");
    const auto first = root / "first.wav";
    const auto second = root / "second.wav";
    const auto third = root / "third.wav";
    ScopedCleanup cleanup(root / "placeholder");

    std::filesystem::create_directories(root);
    {
        std::ofstream out(first);
        REQUIRE(out.good());
    }
    {
        std::ofstream out(second);
        REQUIRE(out.good());
    }
    {
        std::ofstream out(third);
        REQUIRE(out.good());
    }

    cupuacu::persistence::PersistedSessionState state{};
    state.openFiles = {first.string(), second.string(), third.string()};
    state.activeOpenFileIndex = 2;

    const auto plan = cupuacu::actions::planStartupDocumentRestore(
        {third.string(), second.string(), first.string()}, state);

    REQUIRE(plan.recentFiles ==
            std::vector<std::string>{third.string(), second.string(),
                                     first.string()});
    REQUIRE(plan.openFiles ==
            std::vector<std::string>{first.string(), second.string(),
                                     third.string()});
    REQUIRE(plan.activeOpenFileIndex == 2);
    REQUIRE_FALSE(plan.shouldPersistState);
}

TEST_CASE("Startup document restore plan prunes missing recent and open files",
          "[persistence]")
{
    const auto root =
        cupuacu::test::makeUniqueTestRoot("startup-restore-prune");
    const auto existing = root / "keep.wav";
    const auto existingOpen = root / "open.wav";
    const auto missing = root / "missing.wav";
    ScopedCleanup cleanup(root / "placeholder");

    std::filesystem::create_directories(root);
    {
        std::ofstream out(existing);
        REQUIRE(out.good());
    }
    {
        std::ofstream out(existingOpen);
        REQUIRE(out.good());
    }

    cupuacu::persistence::PersistedSessionState state{};
    state.openFiles = {missing.string(), existingOpen.string()};
    state.activeOpenFileIndex = 9;

    const auto plan = cupuacu::actions::planStartupDocumentRestore(
        {"", missing.string(), existing.string()}, state);

    REQUIRE(plan.recentFiles == std::vector<std::string>{existing.string()});
    REQUIRE(plan.openFiles == std::vector<std::string>{existingOpen.string()});
    REQUIRE(plan.activeOpenFileIndex == 0);
    REQUIRE(plan.shouldPersistState);
}

TEST_CASE("Persisted open session state captures open file tabs and the active file-backed tab",
          "[persistence]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.recentFiles = {"/tmp/recent-a.wav", "/tmp/recent-b.wav"};
    state.tabs.resize(3);
    state.tabs[0].session.currentFile = "/tmp/open-a.wav";
    state.tabs[0].viewState.samplesPerPixel = 3.5;
    state.tabs[0].viewState.sampleOffset = 21;
    state.tabs[0].session.selection.setHighest(64.0);
    state.tabs[0].session.selection.setValue1(5.0);
    state.tabs[0].session.selection.setValue2(14.0);
    state.tabs[1].session.currentFile.clear();
    state.tabs[2].session.currentFile = "/tmp/open-b.wav";
    state.tabs[2].viewState.samplesPerPixel = 1.25;
    state.tabs[2].viewState.sampleOffset = 7;
    state.activeTabIndex = 2;

    const auto persisted = cupuacu::actions::buildPersistedOpenSessionState(&state);
    REQUIRE(persisted.openDocuments.size() == 2);
    REQUIRE(persisted.openDocuments[0].filePath == "/tmp/open-a.wav");
    REQUIRE(persisted.openDocuments[0].samplesPerPixel == Catch::Approx(3.5));
    REQUIRE(persisted.openDocuments[0].sampleOffset == 21);
    REQUIRE(persisted.openDocuments[0].selectionStart == 5);
    REQUIRE(persisted.openDocuments[0].selectionEndExclusive == 14);
    REQUIRE(persisted.openDocuments[1].filePath == "/tmp/open-b.wav");
    REQUIRE(persisted.openDocuments[1].samplesPerPixel == Catch::Approx(1.25));
    REQUIRE(persisted.openDocuments[1].sampleOffset == 7);
    REQUIRE_FALSE(persisted.openDocuments[1].selectionStart.has_value());
    REQUIRE(persisted.openFiles ==
            std::vector<std::string>{"/tmp/open-a.wav", "/tmp/open-b.wav"});
    REQUIRE(persisted.activeOpenFileIndex == 1);
}

TEST_CASE("Persisted recent files and session state save to separate files",
          "[persistence]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("session-persist-save");
    ScopedCleanup cleanup(root / "placeholder");

    cupuacu::test::StateWithTestPaths state{root};
    state.recentFiles = {"/tmp/recent-a.wav", "/tmp/recent-b.wav"};
    state.tabs.resize(2);
    state.tabs[0].session.currentFile = "/tmp/open-a.wav";
    state.tabs[1].session.currentFile = "/tmp/open-b.wav";
    state.activeTabIndex = 1;

    cupuacu::actions::persistSessionState(&state);

    const auto loadedRecentFiles = cupuacu::persistence::RecentFilesPersistence::load(
        state.paths->recentlyOpenedFilesPath());
    const auto loadedSession = cupuacu::persistence::SessionStatePersistence::load(
        state.paths->sessionStatePath());

    REQUIRE(loadedRecentFiles ==
            std::vector<std::string>{"/tmp/recent-a.wav", "/tmp/recent-b.wav"});
    REQUIRE(loadedSession.openFiles ==
            std::vector<std::string>{"/tmp/open-a.wav", "/tmp/open-b.wav"});
    REQUIRE(loadedSession.activeOpenFileIndex == 1);
}

TEST_CASE("Persisted session state saves updated zoom and offset for an open document",
          "[persistence]")
{
    const auto root =
        cupuacu::test::makeUniqueTestRoot("session-persist-zoom-save");
    ScopedCleanup cleanup(root / "placeholder");

    cupuacu::test::StateWithTestPaths state{root};
    state.tabs.resize(1);
    state.tabs[0].session.currentFile = "/tmp/open-a.wav";
    state.tabs[0].session.document.initialize(
        cupuacu::SampleFormat::PCM_S16, 44100, 1, 8192);
    state.activeTabIndex = 0;

    state.tabs[0].viewState.samplesPerPixel = 4.0;
    state.tabs[0].viewState.sampleOffset = 1602;

    cupuacu::actions::persistSessionState(&state);

    const auto loadedSession =
        cupuacu::persistence::SessionStatePersistence::load(
            state.paths->sessionStatePath());
    REQUIRE(loadedSession.openDocuments.size() == 1);
    REQUIRE(loadedSession.openDocuments[0].filePath == "/tmp/open-a.wav");
    REQUIRE(loadedSession.openDocuments[0].samplesPerPixel ==
            Catch::Approx(4.0));
    REQUIRE(loadedSession.openDocuments[0].sampleOffset ==
            1602);
}

TEST_CASE("Primary-modifier Q does not apply horizontal zoom-out",
          "[persistence]")
{
    cupuacu::test::StateWithTestPaths state{};
    state.tabs.resize(1);
    state.tabs[0].session.document.initialize(
        cupuacu::SampleFormat::PCM_S16, 44100, 1, 8192);
    state.activeTabIndex = 0;

    auto &viewState = state.getActiveViewState();
    viewState.samplesPerPixel = 4.0;
    viewState.sampleOffset = 64;

    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_Q;
#if __APPLE__
    event.key.mod = SDL_KMOD_GUI;
#else
    event.key.mod = SDL_KMOD_CTRL;
#endif

    cupuacu::gui::handleKeyDown(&event, &state);

    REQUIRE(viewState.samplesPerPixel == Catch::Approx(4.0));
    REQUIRE(viewState.sampleOffset == 64);
}

TEST_CASE("Document lifecycle helpers can skip persistence when requested",
          "[persistence]")
{
    const auto root = cupuacu::test::makeUniqueTestRoot("session-persist-skip");
    ScopedCleanup cleanup(root / "placeholder");

    cupuacu::test::StateWithTestPaths state{root};
    state.recentFiles = {"/tmp/recent-a.wav"};
    state.tabs.resize(1);
    state.tabs[0].session.currentFile = "/tmp/open-a.wav";
    state.activeTabIndex = 0;

    cupuacu::actions::closeCurrentDocument(&state, false);

    REQUIRE_FALSE(
        std::filesystem::exists(state.paths->recentlyOpenedFilesPath()));
    REQUIRE_FALSE(std::filesystem::exists(state.paths->sessionStatePath()));
}
