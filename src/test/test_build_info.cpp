#include <catch2/catch_test_macros.hpp>

#include "BuildInfo.hpp"

TEST_CASE("Build diagnostics contain stable support information", "[build]")
{
    const std::string report =
        cupuacu::build::diagnosticReport("test-renderer");

    REQUIRE(report.find("Cupuacu version: ") != std::string::npos);
    REQUIRE(report.find("Source revision: ") != std::string::npos);
    REQUIRE(report.find("Build configuration: ") != std::string::npos);
    REQUIRE(report.find("Platform: ") != std::string::npos);
    REQUIRE(report.find("Architecture: ") != std::string::npos);
    REQUIRE(report.find("Compiler: ") != std::string::npos);
    REQUIRE(report.find("C++ standard: C++20") != std::string::npos);
    REQUIRE(report.find("SDL renderer: test-renderer") != std::string::npos);
    REQUIRE(report.find("SDL: ") != std::string::npos);
    REQUIRE(report.find("PortAudio: ") != std::string::npos);
    REQUIRE(report.find("libsndfile: ") != std::string::npos);
}

TEST_CASE("Generated credits and notices contain representative dependencies",
          "[build]")
{
    const std::string credits = cupuacu::build::creditsText();
    const std::string notices = cupuacu::build::thirdPartyNotices();

    REQUIRE(credits.find("SDL") != std::string::npos);
    REQUIRE(credits.find("PortAudio") != std::string::npos);
    REQUIRE(credits.find("WebRTC Audio Processing") != std::string::npos);
    REQUIRE(credits.find("DEVELOPMENT AND TESTING") != std::string::npos);
    REQUIRE(credits.find("https://github.com/PortAudio/portaudio/blob/master/"
                         "LICENSE.txt") != std::string::npos);

    REQUIRE(notices.find("SDL (3.2.14)") != std::string::npos);
    REQUIRE(notices.find("PortAudio") != std::string::npos);
    REQUIRE(notices.find("nlohmann/json") != std::string::npos);
}
