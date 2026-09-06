#pragma once

#include "AudioReader.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace cupuacu::storage
{
    inline constexpr int64_t AudioBlockFrames = 65536;
    inline constexpr uint64_t AudioBlockBytes =
        AudioBlockFrames * sizeof(float);

    struct AudioBlock
    {
        uint64_t segment = 0;
        uint64_t offset = 0;
        uint32_t frames = 0;
    };

    // Append-only, process-local working storage. Construction, access and last
    // release belong to workers. This is not a recovery/persistence format.
    class AudioBlockStore
    {
        static inline std::atomic<uint64_t> nextId{1};
        const uint64_t identity = nextId.fetch_add(1);
        std::filesystem::path directory;
        uint64_t segmentLimit;
        mutable std::mutex mutex;
        std::vector<uint64_t> lengths;
        mutable std::ofstream writer;
        mutable std::ifstream reader;
        mutable uint64_t readSegment = UINT64_MAX;
        mutable uint64_t bytesRead = 0;
        uint64_t bytesWritten = 0;
        bool failed = false;

        std::filesystem::path segmentPath(uint64_t index) const
        {
            return directory / ("samples-" + std::to_string(index) + ".bin");
        }

    public:
        explicit AudioBlockStore(std::filesystem::path directoryToUse,
                                 uint64_t segmentBytes = 64 * 1024 * 1024)
            : directory(std::move(directoryToUse)), segmentLimit(segmentBytes)
        {
            if (segmentLimit < AudioBlockBytes ||
                segmentLimit > uint64_t(INT64_MAX))
            {
                throw std::invalid_argument("Invalid audio segment size");
            }
            std::filesystem::create_directories(directory.parent_path());
            if (!std::filesystem::create_directory(directory))
            {
                throw std::runtime_error(
                    "Audio store directory already exists");
            }
        }
        ~AudioBlockStore()
        {
            writer.close();
            reader.close();
            std::error_code ignored;
            std::filesystem::remove_all(directory, ignored);
        }
        uint64_t id() const
        {
            return identity;
        }
        const std::filesystem::path &path() const
        {
            return directory;
        }

        AudioBlock append(std::span<const float> samples)
        {
            if (samples.empty() || samples.size() > AudioBlockFrames)
            {
                throw std::invalid_argument("Invalid audio block length");
            }
            std::lock_guard lock(mutex);
            if (failed)
            {
                throw std::runtime_error("Audio store write previously failed");
            }
            const uint64_t bytes = samples.size_bytes();
            if (lengths.empty() || bytes > segmentLimit - lengths.back())
            {
                if (writer.is_open())
                {
                    writer.close();
                }
                if (writer.fail())
                {
                    failed = true;
                    throw std::runtime_error("Audio segment close failed");
                }
                // A fresh stream preserves normal buffering across segment
                // rollover (reopening a closed libc++ filebuf can lose it).
                writer = std::ofstream(segmentPath(lengths.size()),
                                       std::ios::binary | std::ios::trunc);
                if (!writer)
                {
                    failed = true;
                    throw std::runtime_error("Cannot create audio segment");
                }
                lengths.push_back(0);
            }
            AudioBlock result{lengths.size() - 1, lengths.back(),
                              uint32_t(samples.size())};
            writer.write(reinterpret_cast<const char *>(samples.data()),
                         std::streamsize(bytes));
            if (!writer)
            {
                failed = true;
                throw std::runtime_error("Audio block write failed");
            }
            lengths.back() += bytes;
            bytesWritten += bytes;
            return result;
        }
        void flush()
        {
            std::lock_guard lock(mutex);
            if (writer.is_open())
            {
                writer.flush();
            }
            if (failed || !writer)
            {
                failed = true;
                throw std::runtime_error("Audio store flush failed");
            }
        }
        void read(AudioBlock block, uint32_t start,
                  std::span<float> output) const
        {
            std::lock_guard lock(mutex);
            if (block.frames == 0 || block.frames > AudioBlockFrames ||
                block.segment >= lengths.size() ||
                block.offset > lengths[block.segment] ||
                uint64_t(block.frames) * sizeof(float) >
                    lengths[block.segment] - block.offset ||
                start > block.frames || output.size() > block.frames - start)
            {
                throw std::out_of_range("Invalid audio block reference");
            }
            if (readSegment != block.segment)
            {
                reader.close();
                reader.clear();
                reader =
                    std::ifstream(segmentPath(block.segment), std::ios::binary);
                readSegment = block.segment;
            }
            // A failed append must not poison earlier revisions whose blocks
            // were already flushed before publication. No failed transaction
            // can expose its unflushed block references through finish().
            if (!failed && writer.is_open())
            {
                writer.flush();
                if (!writer)
                {
                    throw std::runtime_error(
                        "Audio store flush failed before read");
                }
            }
            reader.clear();
            reader.seekg(
                std::streamoff(block.offset + uint64_t(start) * sizeof(float)));
            reader.read(reinterpret_cast<char *>(output.data()),
                        std::streamsize(output.size_bytes()));
            if (!reader)
            {
                throw std::runtime_error("Audio block read failed");
            }
            bytesRead += output.size_bytes();
        }
        std::pair<uint64_t, uint64_t> ioBytes() const
        {
            std::lock_guard lock(mutex);
            return {bytesRead, bytesWritten};
        }
    };

    // One cache can serve many revisions/stores. Copies go into caller-owned
    // buffers, so callers cannot pin evicted entries beyond the budget.
    class DecodedBlockCache
    {
        using Samples = std::array<float, AudioBlockFrames>;
        struct Key
        {
            uint64_t store, segment, offset;
            auto operator<=>(const Key &) const = default;
        };
        struct Entry
        {
            Key key;
            std::unique_ptr<Samples> samples;
        };
        mutable std::mutex mutex;
        const uint64_t capacity;
        std::list<Entry> lru;
        std::map<Key, std::list<Entry>::iterator> entries;
        uint64_t hits = 0, misses = 0, peakBytes = 0;

    public:
        struct Stats
        {
            uint64_t residentBytes, peakResidentBytes, hits, misses;
        };
        explicit DecodedBlockCache(uint64_t decodedByteBudget)
            : capacity(decodedByteBudget / AudioBlockBytes)
        {
        }
        static uint64_t defaultByteBudget(uint64_t physicalRamBytes)
        {
            return physicalRamBytes / 10;
        }
        Stats stats() const
        {
            std::lock_guard lock(mutex);
            return {lru.size() * AudioBlockBytes, peakBytes, hits, misses};
        }
        void read(const AudioBlockStore &store, AudioBlock block,
                  uint32_t start, std::span<float> output)
        {
            if (block.frames == 0 || block.frames > AudioBlockFrames ||
                start > block.frames || output.size() > block.frames - start)
            {
                throw std::out_of_range("Read outside audio block");
            }
            if (output.empty())
            {
                return;
            }
            std::lock_guard lock(mutex);
            const Key key{store.id(), block.segment, block.offset};
            auto found = entries.find(key);
            if (found == entries.end())
            {
                ++misses;
                if (capacity == 0)
                {
                    store.read(block, start, output);
                    return;
                }
                std::unique_ptr<Samples> samples;
                if (lru.size() == capacity)
                {
                    samples = std::move(lru.back().samples);
                    entries.erase(lru.back().key);
                    lru.pop_back();
                }
                else
                {
                    samples = std::make_unique<Samples>();
                }
                store.read(block, 0,
                           std::span<float>(*samples).first(block.frames));
                lru.push_front({key, std::move(samples)});
                entries.emplace(key, lru.begin());
                peakBytes = std::max(peakBytes, lru.size() * AudioBlockBytes);
            }
            else
            {
                ++hits;
                lru.splice(lru.begin(), lru, found->second);
            }
            std::copy_n(lru.front().samples->data() + start, output.size(),
                        output.data());
        }
    };
} // namespace cupuacu::storage
