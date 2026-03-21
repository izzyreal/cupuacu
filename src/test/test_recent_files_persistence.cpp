#include <catch2/catch_test_macros.hpp>

#include "TestPaths.hpp"
#include "actions/DocumentLifecycle.hpp"
#include "gui/DevicePropertiesWindow.hpp"
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
    state.openFiles = {"/tmp/open-a.wav", "/tmp/open-b.wav"};
    state.activeOpenFileIndex = 1;

    REQUIRE(cupuacu::persistence::SessionStatePersistence::save(path, state));

    const auto loaded = cupuacu::persistence::SessionStatePersistence::load(path);
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
        out << R"({"version":1,"openFiles":[1,2,3],"activeOpenFileIndex":0})";
    }

    REQUIRE(cupuacu::persistence::SessionStatePersistence::load(path)
                .openFiles.empty());
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
    state.tabs[1].session.currentFile.clear();
    state.tabs[2].session.currentFile = "/tmp/open-b.wav";
    state.activeTabIndex = 2;

    const auto persisted = cupuacu::actions::buildPersistedOpenSessionState(&state);
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
