#include <catch2/catch_test_macros.hpp>

#include "file/SampleQuantization.hpp"

TEST_CASE("Integer PCM quantization matches persisted sample codes", "[file]")
{
    SECTION("edited PCM16 samples use the writer quantization rule")
    {
        REQUIRE(cupuacu::file::quantizeIntegerPcmSample(
                    cupuacu::SampleFormat::PCM_S16, 1.0f, false) == 32767);
        REQUIRE(cupuacu::file::quantizeIntegerPcmSample(
                    cupuacu::SampleFormat::PCM_S16, -1.0f, false) == -32767);
    }

    SECTION("preserved PCM16 samples keep the original loaded code")
    {
        REQUIRE(cupuacu::file::quantizeIntegerPcmSample(
                    cupuacu::SampleFormat::PCM_S16, -1.0f, true) == -32768);
        REQUIRE(cupuacu::file::quantizeIntegerPcmSample(
                    cupuacu::SampleFormat::PCM_S16,
                    32767.0f / 32768.0f, true) == 32767);
    }

    SECTION("PCM8 samples use the exact signed 8-bit range")
    {
        REQUIRE(cupuacu::file::quantizeIntegerPcmSample(
                    cupuacu::SampleFormat::PCM_S8, 1.0f, false) == 127);
        REQUIRE(cupuacu::file::quantizeIntegerPcmSample(
                    cupuacu::SampleFormat::PCM_S8, -1.0f, false) == -127);
        REQUIRE(cupuacu::file::quantizeIntegerPcmSample(
                    cupuacu::SampleFormat::PCM_S8, -1.0f, true) == -128);
    }
}
