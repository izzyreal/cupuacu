#pragma once

#include "../../Document.hpp"
#include "../../storage/AudioReader.hpp"
#include "../AudioFileWriter.hpp"

#include <cstdint>
#include <filesystem>

namespace cupuacu::file::m4a
{
    void writeAlacM4aFile(const storage::AudioReader &audio,
                          const std::vector<DocumentMarker> &markers,
                          const std::filesystem::path &outputPath,
                          std::uint32_t bitDepth = 0,
                          WriteProgressCallback progress = {});
    void writeAlacM4aFile(const cupuacu::Document &document,
                          const std::filesystem::path &outputPath,
                          std::uint32_t bitDepth = 0,
                          cupuacu::file::WriteProgressCallback progress = {});
    void writeAlacM4aFile(const cupuacu::Document::ReadLease &document,
                          const std::filesystem::path &outputPath,
                          std::uint32_t bitDepth = 0,
                          cupuacu::file::WriteProgressCallback progress = {});
} // namespace cupuacu::file::m4a
