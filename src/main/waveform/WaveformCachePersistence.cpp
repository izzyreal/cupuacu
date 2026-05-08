#include "WaveformCachePersistence.hpp"

#include "../DocumentSession.hpp"
#include "../file/FileIo.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>

namespace cupuacu::waveform
{
    namespace
    {
        constexpr char kMagic[] = "CUPUACU_WAVEFORM_CACHE";
        constexpr uint32_t kStorageVersion = 1;
        constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
        constexpr uint64_t kFnvPrime = 1099511628211ull;

        void hashBytes(uint64_t &hash, const void *data, const std::size_t size)
        {
            const auto *bytes = static_cast<const unsigned char *>(data);
            for (std::size_t i = 0; i < size; ++i)
            {
                hash ^= static_cast<uint64_t>(bytes[i]);
                hash *= kFnvPrime;
            }
        }

        template <typename T>
        void hashValue(uint64_t &hash, const T &value)
        {
            hashBytes(hash, &value, sizeof(value));
        }

        void hashString(uint64_t &hash, const std::string &value)
        {
            hashBytes(hash, value.data(), value.size());
        }

        std::string hex64(const uint64_t value)
        {
            constexpr std::array<char, 16> kHexDigits = {
                '0', '1', '2', '3', '4', '5', '6', '7',
                '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
            };

            std::string result(16, '0');
            for (int i = 0; i < 16; ++i)
            {
                const auto shift = static_cast<unsigned>((15 - i) * 4);
                result[static_cast<std::size_t>(i)] =
                    kHexDigits[static_cast<std::size_t>((value >> shift) & 0xfu)];
            }
            return result;
        }

        std::optional<std::string>
        normalizeExistingPath(const std::filesystem::path &path)
        {
            std::error_code ec;
            const auto absolutePath = std::filesystem::absolute(path, ec);
            if (ec)
            {
                return std::nullopt;
            }

            ec.clear();
            const auto canonicalPath = std::filesystem::weakly_canonical(
                absolutePath, ec);
            if (ec)
            {
                return absolutePath.lexically_normal().string();
            }
            return canonicalPath.string();
        }

        void writeU32(std::ostream &output, const uint32_t value)
        {
            const char bytes[] = {
                static_cast<char>(value & 0xffu),
                static_cast<char>((value >> 8) & 0xffu),
                static_cast<char>((value >> 16) & 0xffu),
                static_cast<char>((value >> 24) & 0xffu),
            };
            output.write(bytes, sizeof(bytes));
        }

        void writeU64(std::ostream &output, const uint64_t value)
        {
            for (int shift = 0; shift < 64; shift += 8)
            {
                output.put(static_cast<char>((value >> shift) & 0xffu));
            }
        }

        void writeI64(std::ostream &output, const int64_t value)
        {
            writeU64(output, static_cast<uint64_t>(value));
        }

        void writeFloat(std::ostream &output, const float value)
        {
            uint32_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(bits));
            writeU32(output, bits);
        }

        void writeString(std::ostream &output, const std::string &value)
        {
            if (value.size() > std::numeric_limits<uint32_t>::max())
            {
                throw std::runtime_error("Waveform cache string is too large");
            }
            writeU32(output, static_cast<uint32_t>(value.size()));
            output.write(value.data(), static_cast<std::streamsize>(value.size()));
        }

        uint32_t readU32(std::istream &input)
        {
            unsigned char bytes[4]{};
            input.read(reinterpret_cast<char *>(bytes), sizeof(bytes));
            if (!input)
            {
                throw std::runtime_error("Truncated waveform cache");
            }
            return static_cast<uint32_t>(bytes[0]) |
                   (static_cast<uint32_t>(bytes[1]) << 8) |
                   (static_cast<uint32_t>(bytes[2]) << 16) |
                   (static_cast<uint32_t>(bytes[3]) << 24);
        }

        uint64_t readU64(std::istream &input)
        {
            uint64_t value = 0;
            for (int shift = 0; shift < 64; shift += 8)
            {
                const int byte = input.get();
                if (byte == std::char_traits<char>::eof())
                {
                    throw std::runtime_error("Truncated waveform cache");
                }
                value |= static_cast<uint64_t>(
                             static_cast<unsigned char>(byte))
                         << shift;
            }
            return value;
        }

        int64_t readI64(std::istream &input)
        {
            return static_cast<int64_t>(readU64(input));
        }

        float readFloat(std::istream &input)
        {
            const uint32_t bits = readU32(input);
            float value = 0.0f;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        std::string readString(std::istream &input)
        {
            const uint32_t size = readU32(input);
            std::string value(size, '\0');
            input.read(value.data(), static_cast<std::streamsize>(value.size()));
            if (!input)
            {
                throw std::runtime_error("Truncated waveform cache");
            }
            return value;
        }

        void writePersistentCacheKey(std::ostream &output,
                                     const PersistentCacheKey &key)
        {
            writeString(output, key.sourcePath);
            writeU64(output, key.sourceFileSize);
            writeI64(output, key.sourceLastWriteTimeNs);
            writeU32(output, static_cast<uint32_t>(key.sampleFormat));
            writeU32(output, static_cast<uint32_t>(key.sampleRate));
            writeI64(output, key.channelCount);
            writeI64(output, key.frameCount);
        }

        PersistentCacheKey readPersistentCacheKey(std::istream &input)
        {
            return PersistentCacheKey{
                .sourcePath = readString(input),
                .sourceFileSize = readU64(input),
                .sourceLastWriteTimeNs = readI64(input),
                .sampleFormat = static_cast<SampleFormat>(readU32(input)),
                .sampleRate = static_cast<int>(readU32(input)),
                .channelCount = readI64(input),
                .frameCount = readI64(input),
            };
        }

        bool cacheStateIsComplete(const gui::WaveformCache::BuildState &state,
                                  const int64_t frameCount)
        {
            if (state.numSamples != frameCount || state.dirtyToBlock >= state.dirtyFromBlock)
            {
                return false;
            }
            if (frameCount <= 0)
            {
                return false;
            }
            const int64_t expectedLevel0Size =
                (frameCount + gui::WaveformCache::BASE_BLOCK_SIZE - 1) /
                gui::WaveformCache::BASE_BLOCK_SIZE;
            return !state.levels.empty() &&
                   static_cast<int64_t>(state.levels.front().size()) ==
                       expectedLevel0Size;
        }

        bool sessionHasCompletePersistentCache(
            const cupuacu::DocumentSession &session)
        {
            if (session.document.getChannelCount() <= 0 ||
                session.document.getFrameCount() <= 0 ||
                session.getWaveformCacheBuildProgress().has_value())
            {
                return false;
            }

            const auto frameCount = session.document.getFrameCount();
            for (int64_t channel = 0; channel < session.document.getChannelCount();
                 ++channel)
            {
                const auto state = session.getWaveformCache(
                    static_cast<int>(channel)).snapshotBuildState();
                if (!cacheStateIsComplete(state, frameCount))
                {
                    return false;
                }
            }

            return true;
        }

        void writeCacheFile(const std::filesystem::path &path,
                            const cupuacu::DocumentSession &session,
                            const PersistentCacheKey &key)
        {
            std::ofstream output(path, std::ios::binary);
            if (!output.is_open())
            {
                throw std::runtime_error("Failed to open waveform cache");
            }

            output.write(kMagic, sizeof(kMagic));
            writeU32(output, kStorageVersion);
            writePersistentCacheKey(output, key);
            writeI64(output, session.document.getChannelCount());

            for (int64_t channel = 0; channel < session.document.getChannelCount();
                 ++channel)
            {
                const auto state = session.getWaveformCache(
                    static_cast<int>(channel)).snapshotBuildState();
                writeI64(output, state.numSamples);
                writeI64(output, state.dirtyFromBlock);
                writeI64(output, state.dirtyToBlock);
                writeU32(output, static_cast<uint32_t>(state.levels.size()));
                for (const auto &level : state.levels)
                {
                    writeI64(output, static_cast<int64_t>(level.size()));
                    for (const auto &peak : level)
                    {
                        writeFloat(output, peak.min);
                        writeFloat(output, peak.max);
                    }
                }
            }

            if (!output.good())
            {
                throw std::runtime_error("Failed to write waveform cache");
            }
        }

        std::vector<gui::WaveformCache::BuildResult>
        readCacheResults(std::istream &input, const int64_t channelCount)
        {
            if (channelCount < 0)
            {
                throw std::runtime_error("Invalid waveform cache channel count");
            }

            std::vector<gui::WaveformCache::BuildResult> results;
            results.reserve(static_cast<std::size_t>(channelCount));
            for (int64_t channel = 0; channel < channelCount; ++channel)
            {
                gui::WaveformCache::BuildResult result;
                result.numSamples = readI64(input);
                result.dirtyFromBlock = readI64(input);
                result.dirtyToBlock = readI64(input);
                const auto levelCount = readU32(input);
                result.levels.resize(levelCount);
                for (uint32_t levelIndex = 0; levelIndex < levelCount; ++levelIndex)
                {
                    const auto peakCount = readI64(input);
                    if (peakCount < 0)
                    {
                        throw std::runtime_error("Invalid waveform cache level size");
                    }
                    auto &level = result.levels[static_cast<std::size_t>(levelIndex)];
                    level.resize(static_cast<std::size_t>(peakCount));
                    for (int64_t peakIndex = 0; peakIndex < peakCount; ++peakIndex)
                    {
                        level[static_cast<std::size_t>(peakIndex)] = {
                            .min = readFloat(input),
                            .max = readFloat(input),
                        };
                    }
                }
                results.push_back(std::move(result));
            }

            return results;
        }
    } // namespace

    std::string PersistentCacheKey::cacheBasename() const
    {
        uint64_t hash = kFnvOffsetBasis;
        hashValue(hash, FORMAT_VERSION);
        hashString(hash, sourcePath);
        hashValue(hash, sourceFileSize);
        hashValue(hash, sourceLastWriteTimeNs);
        hashValue(hash, sampleFormat);
        hashValue(hash, sampleRate);
        hashValue(hash, channelCount);
        hashValue(hash, frameCount);
        return hex64(hash) + ".cupuacu-waveform-cache";
    }

    std::filesystem::path
    PersistentCacheKey::cachePath(const Paths &paths) const
    {
        return paths.waveformCachePath() / cacheBasename();
    }

    std::optional<PersistentCacheKey>
    makePersistentCacheKey(const std::string &sourceFilePath,
                           const Document &document)
    {
        if (sourceFilePath.empty() || document.getChannelCount() <= 0 ||
            document.getFrameCount() <= 0)
        {
            return std::nullopt;
        }

        const std::filesystem::path sourcePath(sourceFilePath);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(sourcePath, ec) || ec)
        {
            return std::nullopt;
        }

        const auto normalizedPath = normalizeExistingPath(sourcePath);
        if (!normalizedPath.has_value())
        {
            return std::nullopt;
        }

        ec.clear();
        const auto fileSize = std::filesystem::file_size(sourcePath, ec);
        if (ec)
        {
            return std::nullopt;
        }

        ec.clear();
        const auto writeTime = std::filesystem::last_write_time(sourcePath, ec);
        if (ec)
        {
            return std::nullopt;
        }

        const auto writeTimeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            writeTime.time_since_epoch());

        return PersistentCacheKey{
            .sourcePath = *normalizedPath,
            .sourceFileSize = fileSize,
            .sourceLastWriteTimeNs = writeTimeNs.count(),
            .sampleFormat = document.getSampleFormat(),
            .sampleRate = document.getSampleRate(),
            .channelCount = document.getChannelCount(),
            .frameCount = document.getFrameCount(),
        };
    }

    bool savePersistentWaveformCache(const cupuacu::DocumentSession &session,
                                     const Paths &paths)
    {
        const auto key = session.getPersistentWaveformCacheKey();
        if (!key.has_value() || !sessionHasCompletePersistentCache(session))
        {
            return false;
        }

        try
        {
            cupuacu::file::writeFileAtomically(
                key->cachePath(paths),
                [&](const std::filesystem::path &temporaryPath)
                {
                    writeCacheFile(temporaryPath, session, *key);
                });
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool loadPersistentWaveformCache(cupuacu::DocumentSession &session,
                                     const Paths &paths)
    {
        const auto key = session.getPersistentWaveformCacheKey();
        if (!key.has_value())
        {
            return false;
        }

        try
        {
            std::ifstream input(key->cachePath(paths), std::ios::binary);
            if (!input.is_open())
            {
                return false;
            }

            char magic[sizeof(kMagic)]{};
            input.read(magic, sizeof(magic));
            if (!input || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0)
            {
                return false;
            }
            if (readU32(input) != kStorageVersion)
            {
                return false;
            }

            if (readPersistentCacheKey(input) != *key)
            {
                return false;
            }

            const auto channelCount = readI64(input);
            if (channelCount != session.document.getChannelCount())
            {
                return false;
            }

            auto results = readCacheResults(input, channelCount);
            if (!input)
            {
                return false;
            }

            session.stopWaveformCacheBuild();
            session.waveformCaches.resetToChannelCount(channelCount);
            for (int64_t channel = 0; channel < channelCount; ++channel)
            {
                session.getWaveformCache(static_cast<int>(channel))
                    .applyBuildResult(std::move(
                        results[static_cast<std::size_t>(channel)]));
            }

            return sessionHasCompletePersistentCache(session);
        }
        catch (...)
        {
            return false;
        }
    }
} // namespace cupuacu::waveform
