#pragma once

#include "../State.hpp"
#include "AudioExport.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace cupuacu::file
{
    using WriteProgressCallback =
        std::function<void(const std::string &, std::optional<double>)>;

    class AudioFileWriter
    {
    public:
        static void writeFile(cupuacu::State *state,
                              const std::filesystem::path &outputPath,
                              const AudioExportSettings &settings,
                              WriteProgressCallback progress = {});
        static void writeFile(const cupuacu::Document::ReadLease &document,
                              const std::filesystem::path &outputPath,
                              const AudioExportSettings &settings,
                              WriteProgressCallback progress = {});
    };
} // namespace cupuacu::file
