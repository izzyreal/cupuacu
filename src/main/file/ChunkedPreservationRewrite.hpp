#pragma once

#include "FileIo.hpp"
#include "PcmPreservationIO.hpp"

#include <filesystem>
#include <functional>
#include <fstream>

namespace cupuacu::file::preservation
{
    template <typename CopyPrefixFn, typename WriteChunkFn, typename CopySuffixFn,
              typename RepairFn>
    void rewriteChunkPreserving(const std::filesystem::path &referencePath,
                                const std::filesystem::path &outputPath,
                                CopyPrefixFn copyPrefix, WriteChunkFn writeChunk,
                                CopySuffixFn copySuffix, RepairFn repairOutput)
    {
        writeFileAtomically(
            outputPath,
            [&](const std::filesystem::path &temporaryPath)
            {
                auto input = openInputFileStream(referencePath);
                auto output = openOutputFileStream(temporaryPath);
                copyPrefix(input, output);
                writeChunk(input, output);
                copySuffix(input, output);
                repairOutput(output);
                output.flush();
            });
    }
} // namespace cupuacu::file::preservation
