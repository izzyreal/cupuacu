#pragma once

#include "../Document.hpp"
#include "AudioExport.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace cupuacu::file
{
    using PreservationProgressCallback =
        std::function<void(const std::string &, std::optional<double>)>;

    struct PreservationWriteInput
    {
        const cupuacu::Document::ReadLease &document;
        std::filesystem::path referencePath;
        std::filesystem::path outputPath;
        AudioExportSettings settings;
        PreservationProgressCallback progress;
    };
} // namespace cupuacu::file
