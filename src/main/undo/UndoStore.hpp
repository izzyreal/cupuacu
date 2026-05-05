#pragma once

#include "../Document.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace cupuacu::undo
{
    class UndoStore
    {
    public:
        struct Stats
        {
            std::uint64_t fileCount = 0;
            std::uint64_t totalBytes = 0;
        };

        struct PayloadHandle
        {
            std::filesystem::path path;

            [[nodiscard]] bool empty() const
            {
                return path.empty();
            }
        };

        using SegmentHandle = PayloadHandle;
        using SampleMatrixHandle = PayloadHandle;
        using SampleCubeHandle = PayloadHandle;

        void attach(std::filesystem::path rootPathToUse);
        [[nodiscard]] bool isAttached() const;
        [[nodiscard]] const std::filesystem::path &root() const;
        void clear();

        [[nodiscard]] std::filesystem::path
        allocatePath(const std::string &prefix,
                     const std::string &extension = ".bin") const;

        [[nodiscard]] SegmentHandle
        writeSegment(const cupuacu::Document::AudioSegment &segment,
                     const std::string &prefix = "segment") const;

        [[nodiscard]] cupuacu::Document::AudioSegment
        readSegment(const SegmentHandle &handle) const;

        [[nodiscard]] SampleMatrixHandle
        writeSampleMatrix(
            const std::vector<std::vector<float>> &samples,
            const std::string &prefix = "sample-matrix") const;

        [[nodiscard]] std::vector<std::vector<float>>
        readSampleMatrix(const SampleMatrixHandle &handle) const;

        [[nodiscard]] SampleCubeHandle
        writeSampleCube(
            const std::vector<std::vector<std::vector<float>>> &samples,
            const std::string &prefix = "sample-cube") const;

        [[nodiscard]] std::vector<std::vector<std::vector<float>>>
        readSampleCube(const SampleCubeHandle &handle) const;

        [[nodiscard]] Stats stats() const;

    private:
        std::filesystem::path rootPath;
    };
} // namespace cupuacu::undo
