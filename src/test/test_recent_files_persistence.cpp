#include <catch2/catch_test_macros.hpp>

#include "actions/DocumentLifecycle.hpp"
#include "persistence/RecentFilesPersistence.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace
{
    class TestPaths : public cupuacu::Paths
    {
    public:
        explicit TestPaths(std::filesystem::path rootToUse)
            : root(std::move(rootToUse))
        {
        }

    protected:
        std::filesystem::path appConfigHome() const override
        {
            return root;
        }

    private:
        std::filesystem::path root;
    };

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
    const auto path = std::filesystem::temp_directory_path() /
                      "cupuacu-recent-files-test" /
                      "recently_opened_files.json";
    ScopedCleanup cleanup(path);

    std::vector<std::string> files;
    for (int i = 0; i < 12; ++i)
    {
        files.push_back("/tmp/file-" + std::to_string(i) + ".wav");
    }

    cupuacu::persistence::PersistedSessionState state{};
    state.recentFiles = files;
    state.openFiles = {"/tmp/open-a.wav", "/tmp/open-b.wav"};
    state.activeOpenFileIndex = 1;

    REQUIRE(cupuacu::persistence::RecentFilesPersistence::save(path, state));

    const auto loaded = cupuacu::persistence::RecentFilesPersistence::load(path);
    REQUIRE(loaded.recentFiles.size() == 10);
    REQUIRE(loaded.recentFiles.front() == "/tmp/file-0.wav");
    REQUIRE(loaded.recentFiles.back() == "/tmp/file-9.wav");
    REQUIRE(loaded.openFiles ==
            std::vector<std::string>{"/tmp/open-a.wav", "/tmp/open-b.wav"});
    REQUIRE(loaded.activeOpenFileIndex == 1);
}

TEST_CASE("Recent files persistence rejects malformed payloads", "[persistence]")
{
    const auto path = std::filesystem::temp_directory_path() /
                      "cupuacu-recent-files-invalid" /
                      "recently_opened_files.json";
    ScopedCleanup cleanup(path);

    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream out(path);
        REQUIRE(out.good());
        out << R"({"version":2,"files":["a.wav"]})";
    }

    REQUIRE(cupuacu::persistence::RecentFilesPersistence::load(path)
                .recentFiles.empty());

    {
        std::ofstream out(path);
        REQUIRE(out.good());
        out << R"({"version":2,"recentFiles":[1,2,3],"openFiles":["a.wav"],"activeOpenFileIndex":0})";
    }

    REQUIRE(cupuacu::persistence::RecentFilesPersistence::load(path)
                .recentFiles.empty());
}

TEST_CASE("Recent files persistence still loads the legacy version-1 format",
          "[persistence]")
{
    const auto path = std::filesystem::temp_directory_path() /
                      "cupuacu-recent-files-legacy" /
                      "recently_opened_files.json";
    ScopedCleanup cleanup(path);

    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream out(path);
        REQUIRE(out.good());
        out << R"({"version":1,"files":["/tmp/first.wav","/tmp/second.wav"]})";
    }

    const auto loaded = cupuacu::persistence::RecentFilesPersistence::load(path);
    REQUIRE(loaded.recentFiles ==
            std::vector<std::string>{"/tmp/first.wav", "/tmp/second.wav"});
    REQUIRE(loaded.openFiles.empty());
    REQUIRE(loaded.activeOpenFileIndex == -1);
}

TEST_CASE("Startup document restore plan falls back to the most recent file for legacy state",
          "[persistence]")
{
    const auto root = std::filesystem::temp_directory_path() /
                      "cupuacu-startup-restore-open";
    const auto first = root / "first.wav";
    const auto second = root / "second.wav";
    ScopedCleanup cleanup(root / "placeholder");

    std::filesystem::create_directories(root);
    {
        std::ofstream firstOut(first);
        REQUIRE(firstOut.good());
    }
    {
        std::ofstream secondOut(second);
        REQUIRE(secondOut.good());
    }

    cupuacu::persistence::PersistedSessionState state{};
    state.recentFiles = {first.string(), second.string()};

    const auto plan = cupuacu::actions::planStartupDocumentRestore(state);

    REQUIRE(plan.recentFiles ==
            std::vector<std::string>{first.string(), second.string()});
    REQUIRE(plan.openFiles == std::vector<std::string>{first.string()});
    REQUIRE(plan.activeOpenFileIndex == 0);
}

TEST_CASE("Startup document restore plan restores multiple open files and active tab",
          "[persistence]")
{
    const auto root = std::filesystem::temp_directory_path() /
                      "cupuacu-startup-restore-multi";
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
    state.recentFiles = {third.string(), second.string(), first.string()};
    state.openFiles = {first.string(), second.string(), third.string()};
    state.activeOpenFileIndex = 2;

    const auto plan = cupuacu::actions::planStartupDocumentRestore(state);

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
    const auto root = std::filesystem::temp_directory_path() /
                      "cupuacu-startup-restore-prune";
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
    state.recentFiles = {"", missing.string(), existing.string()};
    state.openFiles = {missing.string(), existingOpen.string()};
    state.activeOpenFileIndex = 9;

    const auto plan = cupuacu::actions::planStartupDocumentRestore(state);

    REQUIRE(plan.recentFiles == std::vector<std::string>{existing.string()});
    REQUIRE(plan.openFiles == std::vector<std::string>{existingOpen.string()});
    REQUIRE(plan.activeOpenFileIndex == 0);
    REQUIRE(plan.shouldPersistState);
}

TEST_CASE("Persisted session state captures open file tabs and the active file-backed tab",
          "[persistence]")
{
    cupuacu::State state{};
    state.recentFiles = {"/tmp/recent-a.wav", "/tmp/recent-b.wav"};
    state.tabs.resize(3);
    state.tabs[0].session.currentFile = "/tmp/open-a.wav";
    state.tabs[1].session.currentFile.clear();
    state.tabs[2].session.currentFile = "/tmp/open-b.wav";
    state.activeTabIndex = 2;

    const auto persisted = cupuacu::actions::buildPersistedSessionState(&state);

    REQUIRE(persisted.recentFiles ==
            std::vector<std::string>{"/tmp/recent-a.wav", "/tmp/recent-b.wav"});
    REQUIRE(persisted.openFiles ==
            std::vector<std::string>{"/tmp/open-a.wav", "/tmp/open-b.wav"});
    REQUIRE(persisted.activeOpenFileIndex == 1);
}

TEST_CASE("Persisted session state round-trips the active open tab index on save",
          "[persistence]")
{
    const auto root = std::filesystem::temp_directory_path() /
                      "cupuacu-session-persist-save";
    ScopedCleanup cleanup(root / "placeholder");

    cupuacu::State state{};
    state.paths = std::make_unique<TestPaths>(root);
    state.recentFiles = {"/tmp/recent-a.wav", "/tmp/recent-b.wav"};
    state.tabs.resize(2);
    state.tabs[0].session.currentFile = "/tmp/open-a.wav";
    state.tabs[1].session.currentFile = "/tmp/open-b.wav";
    state.activeTabIndex = 1;

    cupuacu::actions::persistSessionState(&state);

    const auto loaded = cupuacu::persistence::RecentFilesPersistence::load(
        state.paths->recentlyOpenedFilesPath());
    REQUIRE(loaded.recentFiles ==
            std::vector<std::string>{"/tmp/recent-a.wav", "/tmp/recent-b.wav"});
    REQUIRE(loaded.openFiles ==
            std::vector<std::string>{"/tmp/open-a.wav", "/tmp/open-b.wav"});
    REQUIRE(loaded.activeOpenFileIndex == 1);
}
