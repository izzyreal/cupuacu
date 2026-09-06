#pragma once

#include "AudioReader.hpp"
#include "RecordIndex.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <unordered_map>

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
        RecordIndex<uint64_t> lengths;
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
            lengths.setBack(lengths.back() + bytes);
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
        : public WorkingMemory,
          public std::enable_shared_from_this<DecodedBlockCache>
    {
        struct Key
        {
            uint64_t store, segment, offset;
            uint32_t frames;
            auto operator<=>(const Key &) const = default;
        };
        struct KeyHash
        {
            std::size_t operator()(const Key &key) const
            {
                // Mix aligned disk offsets before bucket selection; their
                // low bits alone would cluster sample and peak pages.
                uint64_t value = key.offset ^ std::rotl(key.segment, 23) ^
                                 std::rotl(key.store, 41) ^ key.frames;
                value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
                value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
                return value ^ (value >> 31);
            }
        };
        struct Entry
        {
            Key key;
            std::unique_ptr<float[]> samples;
            uint64_t bytes;
        };
        mutable std::mutex mutex;
        std::atomic<uint64_t> budget;
        std::condition_variable scratchAvailable;
        unsigned pressure = 0;
        bool protectTransport = false;
        uint64_t workingTarget(MemoryUse use) const
        {
            const auto target = budget.load() >> pressure;
            if (!protectTransport || use == MemoryUse::Transport)
            {
                return target;
            }
            const auto floor = std::min<uint64_t>(16 * 1024 * 1024, target / 4);
            const auto transport = workingByUse[unsigned(MemoryUse::Transport)];
            return target - (floor > transport ? floor - transport : 0);
        }
        uint64_t reserved = 0, inFlight = 0, resident = 0, peakManaged = 0,
                 evictions = 0;
        uint64_t working = 0, rejectedWorking = 0;
        std::array<uint64_t, unsigned(MemoryUse::Count)> workingByUse{};
        uint64_t capacity() const
        {
            const auto target = budget.load() >> pressure;
            const auto pinned = reserved + inFlight;
            return target > pinned ? target - pinned : 0;
        }
        void trim()
        {
            while (resident > capacity())
            {
                resident -= lru.back().bytes;
                entries.erase(lru.back().key);
                lru.pop_back();
                ++evictions;
            }
        }
        void observe()
        {
            peakBytes = std::max(peakBytes, resident + inFlight);
            peakManaged = std::max(peakManaged, resident + inFlight + reserved);
        }
        struct Reservation
        {
            std::shared_ptr<DecodedBlockCache> owner;
            uint64_t bytes = 0;
            MemoryUse use = MemoryUse::Count;
            ~Reservation()
            {
                if (bytes)
                {
                    {
                        std::lock_guard lock(owner->mutex);
                        owner->reserved -= bytes;
                        if (use != MemoryUse::Count)
                        {
                            owner->working -= bytes;
                            owner->workingByUse[unsigned(use)] -= bytes;
                        }
                    }
                    owner->scratchAvailable.notify_all();
                }
            }
        };
        std::list<Entry> lru;
        std::unordered_map<Key, std::list<Entry>::iterator, KeyHash> entries;
        uint64_t hits = 0, misses = 0, peakBytes = 0;

    public:
        struct Stats
        {
            uint64_t residentBytes, peakResidentBytes, hits, misses;
            uint64_t reservedBytes, configuredBytes, targetBytes,
                peakManagedBytes, evictions, inFlightBytes;
            uint64_t workingBytes, rejectedWorkingReservations;
            std::array<uint64_t, unsigned(MemoryUse::Count)> workingByUse;
        };
        explicit DecodedBlockCache(uint64_t decodedByteBudget,
                                   bool reserveTransport = false)
            : budget(decodedByteBudget), protectTransport(reserveTransport)
        {
        }
        static uint64_t defaultByteBudget(uint64_t physicalRamBytes)
        {
            return physicalRamBytes / 10;
        }
        Stats stats() const
        {
            std::lock_guard lock(mutex);
            return {resident + inFlight,
                    peakBytes,
                    hits,
                    misses,
                    reserved,
                    budget.load(),
                    budget.load() >> pressure,
                    peakManaged,
                    evictions,
                    inFlight,
                    working,
                    rejectedWorking,
                    workingByUse};
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
            {
                std::lock_guard lock(mutex);
                pressure = std::min(level, 2u);
                trim();
            }
            scratchAvailable.notify_all();
        }
        // No waiting for another retained buffer to disappear. Callers either
        // use a disk fallback or fail their uncommitted operation. This avoids
        // nested admission waits holding the very memory needed for progress.
        std::shared_ptr<void> tryReserveWorking(uint64_t bytes,
                                                MemoryUse use) override
        {
            if (use >= MemoryUse::Count)
            {
                throw std::invalid_argument("Invalid working memory category");
            }
            auto token = std::make_shared<Reservation>();
            token->owner = shared_from_this();
            std::lock_guard lock(mutex);
            const auto target = workingTarget(use);
            if (reserved > target || inFlight > target - reserved ||
                bytes > target - reserved - inFlight)
            {
                ++rejectedWorking;
                return {};
            }
            reserved += bytes;
            working += bytes;
            workingByUse[unsigned(use)] += bytes;
            token->bytes = bytes;
            token->use = use;
            trim();
            observe();
            return token;
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
                    return working > workingTarget(MemoryUse::Effect) ||
                           bytes > workingTarget(MemoryUse::Effect) - working ||
                           (reserved + inFlight <=
                                workingTarget(MemoryUse::Effect) &&
                            bytes <= workingTarget(MemoryUse::Effect) -
                                         reserved - inFlight);
                });
            if (working > workingTarget(MemoryUse::Effect) ||
                bytes > workingTarget(MemoryUse::Effect) - working)
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
            const Key key{store.id(), block.segment, block.offset,
                          block.frames};
            // Round within bounded size classes. The floor also bounds entry
            // bookkeeping when a store contains many tiny tail blocks.
            const uint64_t allocation = std::bit_ceil(std::max<uint64_t>(
                4096, uint64_t(block.frames) * sizeof(float)));
            std::unique_ptr<float[]> samples;
            bool borrowed = false;
            struct ReleaseRead
            {
                DecodedBlockCache &cache;
                std::unique_ptr<float[]> &samples;
                bool &borrowed;
                uint64_t bytes;
                ~ReleaseRead()
                {
                    if (borrowed)
                    {
                        samples.reset();
                        {
                            std::lock_guard lock(cache.mutex);
                            cache.inFlight -= bytes;
                        }
                        cache.scratchAvailable.notify_all();
                    }
                }
            } release{*this, samples, borrowed, allocation};
            {
                std::lock_guard lock(mutex);
                if (auto found = entries.find(key); found != entries.end())
                {
                    ++hits;
                    lru.splice(lru.begin(), lru, found->second);
                    std::copy_n(lru.front().samples.get() + start,
                                output.size(), output.data());
                    return;
                }
                ++misses;
                const auto available = capacity();
                if (allocation <= available)
                {
                    while (resident > available - allocation)
                    {
                        if (!samples && lru.back().bytes == allocation)
                        {
                            samples = std::move(lru.back().samples);
                        }
                        resident -= lru.back().bytes;
                        entries.erase(lru.back().key);
                        lru.pop_back();
                        ++evictions;
                    }
                    inFlight += allocation;
                    borrowed = true;
                    observe();
                }
            }
            if (borrowed && !samples)
            {
                samples = std::make_unique_for_overwrite<float[]>(
                    allocation / sizeof(float));
            }
            // Disk misses never hold the shared cache lock. Warm reads from
            // another tab and memory reclamation can proceed independently.
            if (!samples)
            {
                store.read(block, start, output);
                return;
            }
            store.read(block, 0, std::span<float>(samples.get(), block.frames));
            std::copy_n(samples.get() + start, output.size(), output.data());
            {
                std::lock_guard lock(mutex);
                const auto target = budget.load() >> pressure;
                const auto pinnedAfter = reserved + inFlight - allocation;
                const auto available =
                    target > pinnedAfter ? target - pinnedAfter : 0;
                if (resident <= available &&
                    allocation <= available - resident &&
                    !entries.contains(key))
                {
                    lru.push_front({key, std::move(samples), allocation});
                    try
                    {
                        entries.emplace(key, lru.begin());
                    }
                    catch (...)
                    {
                        lru.pop_front();
                        throw;
                    }
                    resident += allocation;
                }
                else
                {
                    samples.reset();
                }
                inFlight -= allocation;
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
