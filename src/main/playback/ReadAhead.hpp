#pragma once
#include "../storage/AudioReader.hpp"
#include <array>
#include <atomic>
#include <memory>

namespace cupuacu::playback
{
    // One callback consumer, one I/O producer. Construction is control-thread
    // work. Callback operations only inspect preallocated slots and lock-free
    // atomics; neither a cache miss nor close waits for the reader.
    class ReadAhead
    {
    public:
        static constexpr int64_t blockFrames = 4096;
        static constexpr std::size_t slotCount = 8;
        static constexpr std::size_t sampleBytes =
            slotCount * blockFrames * 2 * sizeof(float);
        explicit ReadAhead(std::shared_ptr<const storage::AudioReader> reader,
                           int64_t initialFrame = 0);
        ~ReadAhead();
        ReadAhead(const ReadAhead &) = delete;
        ReadAhead &operator=(const ReadAhead &) = delete;

        void request(int64_t position, int64_t loopStart = -1) noexcept;
        bool isReady(int64_t frame) noexcept;
        bool readStereo(int64_t frame, float &left, float &right) noexcept;
        void endCallback() noexcept;
        void close() noexcept;
        bool failed() const noexcept;
        bool finished() const noexcept;
        bool hasStarted() const noexcept
        {
            return started;
        }
        storage::AudioShape shape() const noexcept
        {
            return dimensions;
        }

    private:
        enum SlotState : uint32_t
        {
            Empty,
            Writing,
            Ready,
            Reading
        };
        struct Slot
        {
            std::atomic<uint32_t> state{Empty};
            int64_t block = -1;
            std::array<float, blockFrames * 2> samples{};
        };
        struct Shared
        {
            std::array<Slot, slotCount> slots;
            std::atomic<int64_t> wanted{0}, loop{-1};
            std::atomic<bool> closed{false}, failed{false}, finished{false};
        };
        static void run(std::shared_ptr<Shared> shared,
                        std::shared_ptr<const storage::AudioReader> reader);
        storage::AudioShape dimensions;
        std::shared_ptr<Shared> shared;
        int heldSlot = -1; // Callback-owned until endCallback.
        bool started = false;
    };
    static_assert(std::atomic<int64_t>::is_always_lock_free);
    static_assert(std::atomic<uint32_t>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);
} // namespace cupuacu::playback
