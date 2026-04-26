#pragma once

#include "../../Document.hpp"

#include <filesystem>

namespace cupuacu::file::m4a
{
    void writeAlacM4aFile(const cupuacu::Document &document,
                          const std::filesystem::path &outputPath);
} // namespace cupuacu::file::m4a
