#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace cupuacu::file::aiff
{
    struct ChunkInfo
    {
        char id[4]{};
        std::uint32_t payloadSize = 0;
        std::size_t headerOffset = 0;
        std::size_t payloadOffset = 0;
        std::size_t paddedPayloadSize = 0;
    };

    struct ParsedFile
    {
        std::filesystem::path path;
        std::vector<ChunkInfo> chunks;
        std::size_t formSizeOffset = 4;
        std::size_t formDataSize = 0;
        bool isAiff = false;
        bool isPcm16 = false;
        std::size_t commChunkCount = 0;
        std::size_t ssndChunkCount = 0;
        int channelCount = 0;
        int sampleRate = 0;
        int bitsPerSample = 0;
        std::uint32_t sampleFrameCount = 0;
        std::size_t commSampleFrameCountOffset = 0;
        std::uint32_t ssndOffset = 0;
        std::uint32_t ssndBlockSize = 0;
        std::size_t soundDataOffset = 0;
        std::uint32_t soundDataSize = 0;

        [[nodiscard]] const ChunkInfo *findChunk(const char (&chunkId)[5]) const
        {
            for (const auto &chunk : chunks)
            {
                if (chunk.id[0] == chunkId[0] && chunk.id[1] == chunkId[1] &&
                    chunk.id[2] == chunkId[2] && chunk.id[3] == chunkId[3])
                {
                    return &chunk;
                }
            }
            return nullptr;
        }
    };
} // namespace cupuacu::file::aiff
