#pragma once

#include <string>
#include <string_view>

namespace cupuacu::build
{
    std::string applicationVersion();
    std::string sourceDescription();
    std::string buildConfiguration();
    std::string platformDescription();
    std::string architectureDescription();
    std::string compilerDescription();
    std::string runtimeLibraryDescription();

    std::string diagnosticReport(std::string_view rendererName = {});
    std::string aboutText(std::string_view rendererName = {});
    std::string creditsText();
    std::string thirdPartyNotices();
} // namespace cupuacu::build
