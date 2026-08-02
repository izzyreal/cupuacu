#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

TEST_CASE("RTSan verification catches intentional allocation",
          "[rtsan][verification]")
{
    const std::filesystem::path probePath = CUPUACU_RTSAN_ALLOCATION_PROBE_PATH;
    REQUIRE(std::filesystem::exists(probePath));

    const auto logFile = std::filesystem::temp_directory_path() /
                         "cupuacu_rtsan_probe_output.log";
    std::filesystem::remove(logFile);

    const std::string cmd =
        "\"" + probePath.string() + "\" > \"" + logFile.string() + "\" 2>&1";
    const int cmdResult = std::system(cmd.c_str());

    REQUIRE(cmdResult != 0);

    std::ifstream in(logFile);
    REQUIRE(in.good());
    const std::string logContent((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());

    REQUIRE(logContent.find("RealtimeSanitizer") != std::string::npos);
    const bool mentionsUnsafeAllocation =
        logContent.find("unsafe-library-call") != std::string::npos ||
        logContent.find("malloc") != std::string::npos;
    REQUIRE(mentionsUnsafeAllocation);

    in.close();
    std::filesystem::remove(logFile);
}
