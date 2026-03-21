#pragma once

#include "../State.hpp"
#include "AudioExport.hpp"

#include <filesystem>

namespace cupuacu::file
{
    class AudioFileWriter
    {
    public:
        static void writeFile(cupuacu::State *state,
                              const std::filesystem::path &outputPath,
                              const AudioExportSettings &settings);
    };
} // namespace cupuacu::file
