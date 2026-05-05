#include "UndoStore.hpp"

#include "../file/FileIo.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace cupuacu::undo
{
    namespace
    {
        constexpr char kSegmentMagic[] = "CUPUACU_UNDO_SEGMENT";
        constexpr std::uint32_t kSegmentVersion = 1;
        constexpr char kSampleMatrixMagic[] = "CUPUACU_UNDO_SAMPLE_MATRIX";
        constexpr std::uint32_t kSampleMatrixVersion = 1;
        constexpr char kSampleCubeMagic[] = "CUPUACU_UNDO_SAMPLE_CUBE";
        constexpr std::uint32_t kSampleCubeVersion = 1;

        void writeU32(std::ostream &output, const std::uint32_t value)
        {
            const char bytes[] = {
                static_cast<char>(value & 0xffu),
                static_cast<char>((value >> 8) & 0xffu),
                static_cast<char>((value >> 16) & 0xffu),
                static_cast<char>((value >> 24) & 0xffu),
            };
            output.write(bytes, sizeof(bytes));
        }

        void writeI64(std::ostream &output, const std::int64_t value)
        {
            const auto unsignedValue = static_cast<std::uint64_t>(value);
            for (int shift = 0; shift < 64; shift += 8)
            {
                output.put(static_cast<char>((unsignedValue >> shift) & 0xffu));
            }
        }

        void writeU64(std::ostream &output, const std::uint64_t value)
        {
            for (int shift = 0; shift < 64; shift += 8)
            {
                output.put(static_cast<char>((value >> shift) & 0xffu));
            }
        }

        std::uint32_t readU32(std::istream &input)
        {
            unsigned char bytes[4]{};
            input.read(reinterpret_cast<char *>(bytes), sizeof(bytes));
            if (!input)
            {
                throw std::runtime_error("Truncated undo segment");
            }
            return static_cast<std::uint32_t>(bytes[0]) |
                   (static_cast<std::uint32_t>(bytes[1]) << 8) |
                   (static_cast<std::uint32_t>(bytes[2]) << 16) |
                   (static_cast<std::uint32_t>(bytes[3]) << 24);
        }

        std::uint64_t readU64(std::istream &input)
        {
            std::uint64_t value = 0;
            for (int shift = 0; shift < 64; shift += 8)
            {
                const int byte = input.get();
                if (byte == std::char_traits<char>::eof())
                {
                    throw std::runtime_error("Truncated undo segment");
                }
                value |= static_cast<std::uint64_t>(
                             static_cast<unsigned char>(byte))
                         << shift;
            }
            return value;
        }

        std::int64_t readI64(std::istream &input)
        {
            return static_cast<std::int64_t>(readU64(input));
        }

        void writeFloat(std::ostream &output, const float value)
        {
            std::uint32_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(bits));
            writeU32(output, bits);
        }

        float readFloat(std::istream &input)
        {
            const std::uint32_t bits = readU32(input);
            float value = 0.0f;
            static_assert(sizeof(value) == sizeof(bits));
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        cupuacu::SampleFormat sampleFormatFromInt(const std::uint32_t value)
        {
            switch (static_cast<cupuacu::SampleFormat>(value))
            {
                case cupuacu::SampleFormat::PCM_S8:
                case cupuacu::SampleFormat::PCM_S16:
                case cupuacu::SampleFormat::PCM_S24:
                case cupuacu::SampleFormat::PCM_S32:
                case cupuacu::SampleFormat::FLOAT32:
                case cupuacu::SampleFormat::FLOAT64:
                case cupuacu::SampleFormat::Unknown:
                    return static_cast<cupuacu::SampleFormat>(value);
            }
            return cupuacu::SampleFormat::Unknown;
        }

        void writeSegmentFile(const std::filesystem::path &path,
                              const cupuacu::Document::AudioSegment &segment)
        {
            std::ofstream output(path, std::ios::binary);
            if (!output.is_open())
            {
                throw std::runtime_error("Failed to open undo segment");
            }

            output.write(kSegmentMagic, sizeof(kSegmentMagic));
            writeU32(output, kSegmentVersion);
            writeU32(output, static_cast<std::uint32_t>(segment.format));
            writeU32(output, static_cast<std::uint32_t>(segment.sampleRate));
            writeI64(output, segment.channelCount);
            writeI64(output, segment.frameCount);

            for (std::int64_t channel = 0; channel < segment.channelCount; ++channel)
            {
                const auto &channelSamples =
                    segment.samples[static_cast<std::size_t>(channel)];
                const auto &channelProvenance =
                    segment.provenance[static_cast<std::size_t>(channel)];
                for (std::int64_t frame = 0; frame < segment.frameCount; ++frame)
                {
                    writeFloat(output,
                               channelSamples[static_cast<std::size_t>(frame)]);
                    const auto &provenance =
                        channelProvenance[static_cast<std::size_t>(frame)];
                    writeU64(output, provenance.sourceId);
                    writeI64(output, provenance.frameIndex);
                }
            }

            if (!output.good())
            {
                throw std::runtime_error("Failed to write undo segment");
            }
        }

        cupuacu::Document::AudioSegment readSegmentFile(std::istream &input)
        {
            char magic[sizeof(kSegmentMagic)]{};
            input.read(magic, sizeof(magic));
            if (!input || std::memcmp(magic, kSegmentMagic, sizeof(kSegmentMagic)) !=
                              0)
            {
                throw std::runtime_error("Invalid undo segment");
            }
            if (readU32(input) != kSegmentVersion)
            {
                throw std::runtime_error("Unsupported undo segment version");
            }

            cupuacu::Document::AudioSegment segment{};
            segment.format = sampleFormatFromInt(readU32(input));
            segment.sampleRate = static_cast<int>(readU32(input));
            segment.channelCount = readI64(input);
            segment.frameCount = readI64(input);
            if (segment.channelCount < 0 || segment.frameCount < 0)
            {
                throw std::runtime_error("Invalid undo segment dimensions");
            }

            segment.samples.assign(
                static_cast<std::size_t>(segment.channelCount), {});
            segment.provenance.assign(
                static_cast<std::size_t>(segment.channelCount), {});

            for (std::int64_t channel = 0; channel < segment.channelCount; ++channel)
            {
                auto &channelSamples =
                    segment.samples[static_cast<std::size_t>(channel)];
                auto &channelProvenance =
                    segment.provenance[static_cast<std::size_t>(channel)];
                channelSamples.resize(static_cast<std::size_t>(segment.frameCount));
                channelProvenance.resize(
                    static_cast<std::size_t>(segment.frameCount));
                for (std::int64_t frame = 0; frame < segment.frameCount; ++frame)
                {
                    channelSamples[static_cast<std::size_t>(frame)] =
                        readFloat(input);
                    channelProvenance[static_cast<std::size_t>(frame)] = {
                        .sourceId = readU64(input),
                        .frameIndex = readI64(input),
                    };
                }
            }

            return segment;
        }

        void writeSampleMatrixFile(
            const std::filesystem::path &path,
            const std::vector<std::vector<float>> &samples)
        {
            std::ofstream output(path, std::ios::binary);
            if (!output.is_open())
            {
                throw std::runtime_error("Failed to open undo sample matrix");
            }

            output.write(kSampleMatrixMagic, sizeof(kSampleMatrixMagic));
            writeU32(output, kSampleMatrixVersion);
            writeU64(output, samples.size());
            for (const auto &channel : samples)
            {
                writeU64(output, channel.size());
                for (const float sample : channel)
                {
                    writeFloat(output, sample);
                }
            }

            if (!output.good())
            {
                throw std::runtime_error("Failed to write undo sample matrix");
            }
        }

        std::vector<std::vector<float>> readSampleMatrixFile(std::istream &input)
        {
            char magic[sizeof(kSampleMatrixMagic)]{};
            input.read(magic, sizeof(magic));
            if (!input || std::memcmp(magic, kSampleMatrixMagic,
                                      sizeof(kSampleMatrixMagic)) != 0)
            {
                throw std::runtime_error("Invalid undo sample matrix");
            }
            if (readU32(input) != kSampleMatrixVersion)
            {
                throw std::runtime_error(
                    "Unsupported undo sample matrix version");
            }

            std::vector<std::vector<float>> samples(readU64(input));
            for (auto &channel : samples)
            {
                channel.resize(readU64(input));
                for (auto &sample : channel)
                {
                    sample = readFloat(input);
                }
            }
            return samples;
        }

        void writeSampleCubeFile(
            const std::filesystem::path &path,
            const std::vector<std::vector<std::vector<float>>> &samples)
        {
            std::ofstream output(path, std::ios::binary);
            if (!output.is_open())
            {
                throw std::runtime_error("Failed to open undo sample cube");
            }

            output.write(kSampleCubeMagic, sizeof(kSampleCubeMagic));
            writeU32(output, kSampleCubeVersion);
            writeU64(output, samples.size());
            for (const auto &matrix : samples)
            {
                writeU64(output, matrix.size());
                for (const auto &channel : matrix)
                {
                    writeU64(output, channel.size());
                    for (const float sample : channel)
                    {
                        writeFloat(output, sample);
                    }
                }
            }

            if (!output.good())
            {
                throw std::runtime_error("Failed to write undo sample cube");
            }
        }

        std::vector<std::vector<std::vector<float>>>
        readSampleCubeFile(std::istream &input)
        {
            char magic[sizeof(kSampleCubeMagic)]{};
            input.read(magic, sizeof(magic));
            if (!input || std::memcmp(magic, kSampleCubeMagic,
                                      sizeof(kSampleCubeMagic)) != 0)
            {
                throw std::runtime_error("Invalid undo sample cube");
            }
            if (readU32(input) != kSampleCubeVersion)
            {
                throw std::runtime_error("Unsupported undo sample cube version");
            }

            std::vector<std::vector<std::vector<float>>> samples(readU64(input));
            for (auto &matrix : samples)
            {
                matrix.resize(readU64(input));
                for (auto &channel : matrix)
                {
                    channel.resize(readU64(input));
                    for (auto &sample : channel)
                    {
                        sample = readFloat(input);
                    }
                }
            }
            return samples;
        }
    } // namespace

    void UndoStore::attach(std::filesystem::path rootPathToUse)
    {
        rootPath = std::move(rootPathToUse);
        if (rootPath.empty())
        {
            return;
        }

        cupuacu::file::ensureParentDirectoryExists(rootPath / "placeholder");

        std::error_code ec;
        std::filesystem::create_directories(rootPath, ec);
        if (ec)
        {
            throw std::runtime_error("Failed to create undo store directory: " +
                                     ec.message());
        }
    }

    bool UndoStore::isAttached() const
    {
        return !rootPath.empty();
    }

    const std::filesystem::path &UndoStore::root() const
    {
        return rootPath;
    }

    void UndoStore::clear()
    {
        if (rootPath.empty())
        {
            return;
        }

        std::error_code ec;
        std::filesystem::remove_all(rootPath, ec);
        rootPath.clear();
    }

    std::filesystem::path UndoStore::allocatePath(
        const std::string &prefix, const std::string &extension) const
    {
        if (rootPath.empty())
        {
            return {};
        }

        static std::atomic<uint64_t> counter{0};
        const auto tick = static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        const auto index = counter.fetch_add(1);
        return rootPath / (prefix + "-" + std::to_string(tick) + "-" +
                           std::to_string(index) + extension);
    }

    auto UndoStore::writeSegment(const cupuacu::Document::AudioSegment &segment,
                                 const std::string &prefix) const
        -> SegmentHandle
    {
        const auto path = allocatePath(prefix, ".cupuacu-undo-segment");
        if (path.empty())
        {
            throw std::runtime_error("Undo store is not attached");
        }

        cupuacu::file::writeFileAtomically(
            path,
            [&](const std::filesystem::path &temporaryPath)
            {
                writeSegmentFile(temporaryPath, segment);
            });
        return {.path = path};
    }

    cupuacu::Document::AudioSegment
    UndoStore::readSegment(const SegmentHandle &handle) const
    {
        if (handle.empty())
        {
            throw std::runtime_error("Undo segment handle is empty");
        }

        std::ifstream input(handle.path, std::ios::binary);
        if (!input.is_open())
        {
            throw std::runtime_error("Failed to open undo segment");
        }
        return readSegmentFile(input);
    }

    auto UndoStore::writeSampleMatrix(
        const std::vector<std::vector<float>> &samples,
        const std::string &prefix) const -> SampleMatrixHandle
    {
        const auto path = allocatePath(prefix, ".cupuacu-undo-sample-matrix");
        if (path.empty())
        {
            throw std::runtime_error("Undo store is not attached");
        }

        cupuacu::file::writeFileAtomically(
            path,
            [&](const std::filesystem::path &temporaryPath)
            {
                writeSampleMatrixFile(temporaryPath, samples);
            });
        return {.path = path};
    }

    std::vector<std::vector<float>>
    UndoStore::readSampleMatrix(const SampleMatrixHandle &handle) const
    {
        if (handle.empty())
        {
            throw std::runtime_error("Undo sample matrix handle is empty");
        }

        std::ifstream input(handle.path, std::ios::binary);
        if (!input.is_open())
        {
            throw std::runtime_error("Failed to open undo sample matrix");
        }
        return readSampleMatrixFile(input);
    }

    auto UndoStore::writeSampleCube(
        const std::vector<std::vector<std::vector<float>>> &samples,
        const std::string &prefix) const -> SampleCubeHandle
    {
        const auto path = allocatePath(prefix, ".cupuacu-undo-sample-cube");
        if (path.empty())
        {
            throw std::runtime_error("Undo store is not attached");
        }

        cupuacu::file::writeFileAtomically(
            path,
            [&](const std::filesystem::path &temporaryPath)
            {
                writeSampleCubeFile(temporaryPath, samples);
            });
        return {.path = path};
    }

    std::vector<std::vector<std::vector<float>>>
    UndoStore::readSampleCube(const SampleCubeHandle &handle) const
    {
        if (handle.empty())
        {
            throw std::runtime_error("Undo sample cube handle is empty");
        }

        std::ifstream input(handle.path, std::ios::binary);
        if (!input.is_open())
        {
            throw std::runtime_error("Failed to open undo sample cube");
        }
        return readSampleCubeFile(input);
    }

    auto UndoStore::stats() const -> Stats
    {
        Stats result{};
        if (rootPath.empty())
        {
            return result;
        }

        std::error_code ec;
        if (!std::filesystem::exists(rootPath, ec) || ec)
        {
            return result;
        }

        for (const auto &entry : std::filesystem::directory_iterator(rootPath, ec))
        {
            if (ec)
            {
                break;
            }
            if (!entry.is_regular_file())
            {
                continue;
            }

            ++result.fileCount;
            const auto size = entry.file_size(ec);
            if (!ec)
            {
                result.totalBytes += static_cast<std::uint64_t>(size);
            }
            else
            {
                ec.clear();
            }
        }
        return result;
    }
} // namespace cupuacu::undo
