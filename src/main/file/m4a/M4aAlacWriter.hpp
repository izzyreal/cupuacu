#pragma once

#include "../../Document.hpp"

#include <cstdint>
#include <filesystem>

namespace cupuacu::file::m4a
{
    void writeAlacM4aFile(const cupuacu::Document &document,
                          const std::filesystem::path &outputPath,
                          std::uint32_t bitDepth = 0);
} // namespace cupuacu::file::m4a
