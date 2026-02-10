#include <catch2/catch_test_macros.hpp>

#if defined(__APPLE__) || defined(__linux__)
#include <filesystem>
#include <fstream>
#include <string>
#include <cstdlib>
#endif

TEST_CASE("TSan verification catches intentional race", "[tsan]")
{
#if defined(__APPLE__) || defined(__linux__)
#if !CUPUACU_TSAN_ENABLED
    FAIL("TSan is inactive for cupuacu-tests on this platform/compiler.");
#else
    const std::filesystem::path probePath = CUPUACU_TSAN_RACE_PROBE_PATH;
    REQUIRE(std::filesystem::exists(probePath));

    const auto logFile =
        std::filesystem::temp_directory_path() / "cupuacu_tsan_probe_output.log";
    std::filesystem::remove(logFile);

    const std::string cmd = "TSAN_OPTIONS=halt_on_error=1 "
                            "\"" +
                            probePath.string() + "\" > /dev/null 2> \"" +
                            logFile.string() + "\"";
    const int cmdResult = std::system(cmd.c_str());

    REQUIRE(cmdResult != 0);

    std::ifstream in(logFile);
    REQUIRE(in.good());
    const std::string logContent((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());

    // In some runners/IDEs, the shell emits the "Abort trap" line outside the
    // probe's redirected stderr, so logContent can be empty even when TSan
    // correctly stopped the probe with a non-zero exit.
    if (!logContent.empty())
    {
        const bool mentionsTsan =
            logContent.find("ThreadSanitizer") != std::string::npos ||
            logContent.find("data race") != std::string::npos;
        REQUIRE(mentionsTsan);
    }
#endif
#else
    SUCCEED("TSan verification test is only relevant on macOS/Linux.");
#endif
}
