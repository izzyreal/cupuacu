#pragma once

#include "AudioReader.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
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
        friend class RevisionArchive;
        bool removeOnDestroy = true;
        std::shared_ptr<const void> archiveOwner;
        AudioBlockStore() : segmentLimit(64 * 1024 * 1024) {}
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
            if (removeOnDestroy)
            {
                std::filesystem::remove_all(directory, ignored);
            }
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
        : public std::enable_shared_from_this<DecodedBlockCache>
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
        std::atomic<uint64_t> budget;
        std::condition_variable scratchAvailable;
        unsigned pressure = 0;
        uint64_t reserved = 0, inFlight = 0, peakManaged = 0, evictions = 0;
        uint64_t capacity() const
        {
            const auto target = budget.load() >> pressure;
            const auto pinned = reserved + inFlight * AudioBlockBytes;
            return target > pinned ? (target - pinned) / AudioBlockBytes : 0;
        }
        void trim()
        {
            while (lru.size() > capacity())
            {
                entries.erase(lru.back().key);
                lru.pop_back();
                ++evictions;
            }
        }
        void observe()
        {
            peakBytes =
                std::max(peakBytes, (lru.size() + inFlight) * AudioBlockBytes);
            peakManaged =
                std::max(peakManaged,
                         (lru.size() + inFlight) * AudioBlockBytes + reserved);
        }
        struct Reservation
        {
            std::shared_ptr<DecodedBlockCache> owner;
            uint64_t bytes = 0;
            ~Reservation()
            {
                if (bytes)
                {
                    {
                        std::lock_guard lock(owner->mutex);
                        owner->reserved -= bytes;
                    }
                    owner->scratchAvailable.notify_all();
                }
            }
        };
        std::list<Entry> lru;
        std::map<Key, std::list<Entry>::iterator> entries;
        uint64_t hits = 0, misses = 0, peakBytes = 0;

    public:
        struct Stats
        {
            uint64_t residentBytes, peakResidentBytes, hits, misses;
            uint64_t reservedBytes, configuredBytes, targetBytes,
                peakManagedBytes, evictions, inFlightBytes;
        };
        explicit DecodedBlockCache(uint64_t decodedByteBudget)
            : budget(decodedByteBudget)
        {
        }
        static uint64_t defaultByteBudget(uint64_t physicalRamBytes)
        {
            return physicalRamBytes / 10;
        }
        Stats stats() const
        {
            std::lock_guard lock(mutex);
            return {(lru.size() + inFlight) * AudioBlockBytes,
                    peakBytes,
                    hits,
                    misses,
                    reserved,
                    budget.load(),
                    budget.load() >> pressure,
                    peakManaged,
                    evictions,
                    inFlight * AudioBlockBytes};
        }
        uint64_t byteBudget() const
        {
            return budget.load();
        }
        // Worker-only: trimming may release many allocations and wait for a
        // read.
        void setByteBudget(uint64_t bytes)
        {
            {
                std::lock_guard lock(mutex);
                budget.store(bytes);
                trim();
            }
            scratchAvailable.notify_all();
        }
        // Normal / warning / critical. Existing job reservations remain valid;
        // pressure sacrifices cache capacity first, never accepted edits.
        void setPressure(unsigned level)
        {
            std::lock_guard lock(mutex);
            pressure = std::min(level, 2u);
            trim();
        }
        std::shared_ptr<void> reserveScratch(uint64_t bytes)
        {
            if (!bytes)
            {
                return {};
            }
            auto token = std::make_shared<Reservation>();
            token->owner = shared_from_this();
            std::unique_lock lock(mutex);
            scratchAvailable.wait(
                lock,
                [&]
                {
                    return bytes > budget.load() ||
                           (reserved + inFlight * AudioBlockBytes <=
                                budget.load() &&
                            bytes <= budget.load() - reserved -
                                         inFlight * AudioBlockBytes);
                });
            if (bytes > budget.load())
            {
                throw std::runtime_error(
                    "Job scratch exceeds the audio memory budget");
            }
            reserved += bytes;
            token->bytes = bytes;
            trim();
            observe();
            return token;
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
            const Key key{store.id(), block.segment, block.offset};
            std::unique_ptr<Samples> samples;
            bool borrowed = false;
            struct ReleaseRead
            {
                DecodedBlockCache &cache;
                std::unique_ptr<Samples> &samples;
                bool &borrowed;
                ~ReleaseRead()
                {
                    if (borrowed)
                    {
                        samples.reset();
                        {
                            std::lock_guard lock(cache.mutex);
                            --cache.inFlight;
                        }
                        cache.scratchAvailable.notify_all();
                    }
                }
            } release{*this, samples, borrowed};
            {
                std::lock_guard lock(mutex);
                if (auto found = entries.find(key); found != entries.end())
                {
                    ++hits;
                    lru.splice(lru.begin(), lru, found->second);
                    std::copy_n(lru.front().samples->data() + start,
                                output.size(), output.data());
                    return;
                }
                ++misses;
                if (capacity())
                {
                    if (lru.size() == capacity())
                    {
                        samples = std::move(lru.back().samples);
                        entries.erase(lru.back().key);
                        lru.pop_back();
                        ++evictions;
                    }
                    else
                    {
                        samples = std::make_unique<Samples>();
                    }
                    ++inFlight;
                    borrowed = true;
                    observe();
                }
            }
            // Disk misses never hold the shared cache lock. Warm reads from
            // another tab and memory reclamation can proceed independently.
            if (!samples)
            {
                store.read(block, start, output);
                return;
            }
            store.read(block, 0,
                       std::span<float>(*samples).first(block.frames));
            std::copy_n(samples->data() + start, output.size(), output.data());
            {
                std::lock_guard lock(mutex);
                // The read's own slot becomes available when it is published.
                const auto target = budget.load() >> pressure;
                const auto pinnedAfter =
                    reserved + (inFlight - 1) * AudioBlockBytes;
                const auto slots =
                    target > pinnedAfter
                        ? (target - pinnedAfter) / AudioBlockBytes
                        : 0;
                if (lru.size() < slots && !entries.contains(key))
                {
                    lru.push_front({key, std::move(samples)});
                    try
                    {
                        entries.emplace(key, lru.begin());
                    }
                    catch (...)
                    {
                        lru.pop_front();
                        throw;
                    }
                }
                else
                {
                    samples.reset();
                }
                --inFlight;
                borrowed = false;
            }
            scratchAvailable.notify_all();
        }
    };
    // One application cache for imports, effects, recordings, clipboard and
    // recovery. Explicit cache injection remains available for isolated
    // readers.
    std::shared_ptr<DecodedBlockCache> defaultDecodedBlockCache();
} // namespace cupuacu::storage
