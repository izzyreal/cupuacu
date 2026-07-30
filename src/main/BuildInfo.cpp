#include "BuildInfo.hpp"

#include "BuildInfoGenerated.hpp"
#include "ResourceUtil.hpp"

#include <SDL3/SDL.h>
#include <portaudio.h>
#include <sndfile.h>

#include <array>
#include <sstream>

#ifndef CUPUACU_BUILD_CONFIGURATION
#define CUPUACU_BUILD_CONFIGURATION "unknown"
#endif

namespace
{
    std::string sdlVersion()
    {
        const int version = SDL_GetVersion();
        std::ostringstream result;
        result << SDL_VERSIONNUM_MAJOR(version) << '.'
               << SDL_VERSIONNUM_MINOR(version) << '.'
               << SDL_VERSIONNUM_MICRO(version);
        return result.str();
    }

    std::string portAudioVersion()
    {
        const PaVersionInfo *version = Pa_GetVersionInfo();
        if (!version)
        {
            return "unknown";
        }

        std::ostringstream result;
        result << version->versionMajor << '.' << version->versionMinor << '.'
               << version->versionSubMinor;
        if (version->versionControlRevision &&
            version->versionControlRevision[0] != '\0')
        {
            result << " (" << version->versionControlRevision << ')';
        }
        return result.str();
    }

    std::string libsndfileVersion()
    {
        std::array<char, 128> buffer{};
        const int result =
            sf_command(nullptr, SFC_GET_LIB_VERSION, buffer.data(),
                       static_cast<int>(buffer.size()));
        return result == 0 || buffer.front() == '\0'
                   ? "unknown"
                   : std::string(buffer.data());
    }
} // namespace

std::string cupuacu::build::applicationVersion()
{
    return std::string(version);
}

std::string cupuacu::build::sourceDescription()
{
    std::string result(sourceRevisionShort);
    if (sourceDirty)
    {
        result += " (modified)";
    }
    return result;
}

std::string cupuacu::build::buildConfiguration()
{
    const std::string configured = CUPUACU_BUILD_CONFIGURATION;
    return configured.empty() ? "unknown" : configured;
}

std::string cupuacu::build::platformDescription()
{
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#elif defined(__FreeBSD__)
    return "FreeBSD";
#else
    return "Unknown";
#endif
}

std::string cupuacu::build::architectureDescription()
{
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#else
    return "unknown";
#endif
}

std::string cupuacu::build::compilerDescription()
{
    return std::string(configuredCompiler);
}

std::string cupuacu::build::runtimeLibraryDescription()
{
    std::ostringstream report;
    report << "SDL: " << sdlVersion() << '\n'
           << "PortAudio: " << portAudioVersion() << '\n'
           << "libsndfile: " << libsndfileVersion();
    return report.str();
}

std::string
cupuacu::build::diagnosticReport(const std::string_view rendererName)
{
    std::ostringstream report;
    report << "Cupuacu version: " << applicationVersion() << '\n'
           << "Source revision: " << sourceDescription() << '\n'
           << "Build configuration: " << buildConfiguration() << '\n'
           << "Platform: " << platformDescription() << '\n'
           << "Architecture: " << architectureDescription() << '\n'
           << "Compiler: " << compilerDescription() << '\n'
           << "C++ standard: C++20\n";
    if (!rendererName.empty())
    {
        report << "SDL renderer: " << rendererName << '\n';
    }
    report << runtimeLibraryDescription() << '\n';
    return report.str();
}

std::string cupuacu::build::aboutText(const std::string_view rendererName)
{
    std::ostringstream text;
    text << "ABOUT\n\n"
         << "Cupuacu is a minimalist cross-platform audio editor inspired by "
            "Syntrillium Cool Edit.\n\n"
         << "Its name comes from cupuaçu, an uncommon tropical fruit, and was "
            "chosen as a nod to Cool Edit.\n\n"
         << "Copyright 2025 Izmar Verhage\n"
         << "Cupuacu is distributed under the MIT License.\n\n"
         << "BUILD DETAILS\n\n"
         << diagnosticReport(rendererName);
    return text.str();
}

std::string cupuacu::build::creditsText()
{
    return get_generated_resource_data("THIRD_PARTY_CREDITS.txt");
}

std::string cupuacu::build::thirdPartyNotices()
{
    return get_generated_resource_data("THIRD_PARTY_NOTICES.txt");
}
