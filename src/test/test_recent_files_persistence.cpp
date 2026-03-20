#include <catch2/catch_test_macros.hpp>

#include "actions/DocumentLifecycle.hpp"
#include "persistence/RecentFilesPersistence.hpp"

#include <filesystem>
#include <fstream>
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
    const auto path = std::filesystem::temp_directory_path() /
                      "cupuacu-recent-files-test" /
                      "recently_opened_files.json";
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

    REQUIRE(cupuacu::persistence::RecentFilesPersistence::load(path).empty());

    {
        std::ofstream out(path);
        REQUIRE(out.good());
        out << R"({"version":1,"files":[1,2,3]})";
    }

    REQUIRE(cupuacu::persistence::RecentFilesPersistence::load(path).empty());
}

TEST_CASE("Startup document restore plan reopens the most recent existing file",
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

    const auto plan = cupuacu::actions::planStartupDocumentRestore(
        {first.string(), second.string()});

    REQUIRE(plan.fileToOpen == first.string());
    REQUIRE(plan.recentFiles ==
            std::vector<std::string>{first.string(), second.string()});
    REQUIRE_FALSE(plan.shouldPersistRecentFiles);
}

TEST_CASE("Startup document restore plan prunes missing recent files",
          "[persistence]")
{
    const auto root = std::filesystem::temp_directory_path() /
                      "cupuacu-startup-restore-prune";
    const auto existing = root / "keep.wav";
    const auto missing = root / "missing.wav";
    ScopedCleanup cleanup(root / "placeholder");

    std::filesystem::create_directories(root);
    {
        std::ofstream out(existing);
        REQUIRE(out.good());
    }

    const auto plan = cupuacu::actions::planStartupDocumentRestore(
        {"", missing.string(), existing.string()});

    REQUIRE(plan.fileToOpen.empty());
    REQUIRE(plan.recentFiles == std::vector<std::string>{existing.string()});
    REQUIRE(plan.shouldPersistRecentFiles);
}
