#include "ReadAhead.hpp"
#include <algorithm>
#include <chrono>
#include <thread>

namespace cupuacu::playback
{
    ReadAhead::ReadAhead(std::shared_ptr<const storage::AudioReader> reader,
                         int64_t initialFrame)
        : dimensions(reader ? reader->shape() : storage::AudioShape{}),
          shared(std::make_shared<Shared>())
    {
        if (!reader || dimensions.frames <= 0 || dimensions.channels < 1 ||
            dimensions.channels > 2 || initialFrame < 0 ||
            initialFrame >= dimensions.frames)
        {
            throw std::invalid_argument(
                "Invalid playback reader or starting frame");
        }
        shared->wanted.store(initialFrame / blockFrames,
                             std::memory_order_relaxed);
        std::thread(&ReadAhead::run, shared, std::move(reader)).detach();
    }
    ReadAhead::~ReadAhead()
    {
        close();
    }
    void ReadAhead::close() noexcept
    {
        shared->closed.store(true, std::memory_order_release);
    }
    bool ReadAhead::failed() const noexcept
    {
        return shared->failed.load(std::memory_order_acquire);
    }
    bool ReadAhead::finished() const noexcept
    {
        return shared->finished.load(std::memory_order_acquire);
    }
    void ReadAhead::request(int64_t position, int64_t loopStart) noexcept
    {
        shared->wanted.store(position < 0 ? -1 : position / blockFrames,
                             std::memory_order_release);
        shared->loop.store(loopStart < 0 ? -1 : loopStart / blockFrames,
                           std::memory_order_release);
    }
    void ReadAhead::endCallback() noexcept
    {
        if (heldSlot >= 0)
        {
            shared->slots[heldSlot].state.store(Ready,
                                                std::memory_order_release);
            heldSlot = -1;
        }
    }
    bool ReadAhead::isReady(int64_t frame) noexcept
    {
        if (frame < 0 || frame >= dimensions.frames)
        {
            return false;
        }
        const auto block = frame / blockFrames;
        if (heldSlot >= 0 && shared->slots[heldSlot].block == block)
        {
            return true;
        }
        for (auto &slot : shared->slots)
        {
            uint32_t expected = Ready;
            if (!slot.state.compare_exchange_strong(expected, Reading,
                                                    std::memory_order_acquire,
                                                    std::memory_order_relaxed))
            {
                continue;
            }
            const bool found = slot.block == block;
            slot.state.store(Ready, std::memory_order_release);
            if (found)
            {
                return true;
            }
        }
        return false;
    }

    bool ReadAhead::readStereo(int64_t frame, float &left,
                               float &right) noexcept
    {
        if (frame < 0 || frame >= dimensions.frames)
        {
            return false;
        }
        const auto block = frame / blockFrames;
        if (heldSlot >= 0 && shared->slots[heldSlot].block != block)
        {
            endCallback();
        }
        if (heldSlot < 0)
        {
            shared->wanted.store(block, std::memory_order_release);
            for (std::size_t i = 0; i < slotCount; ++i)
            {
                uint32_t expected = Ready;
                auto &slot = shared->slots[i];
                if (!slot.state.compare_exchange_strong(
                        expected, Reading, std::memory_order_acquire,
                        std::memory_order_relaxed))
                {
                    continue;
                }
                if (slot.block == block)
                {
                    heldSlot = int(i);
                    break;
                }
                slot.state.store(Ready, std::memory_order_release);
            }
        }
        if (heldSlot < 0)
        {
            return false;
        }
        const auto offset = std::size_t(frame % blockFrames) * 2;
        const auto &samples = shared->slots[heldSlot].samples;
        left = samples[offset];
        right = samples[offset + 1];
        started = true;
        return true;
    }
    void ReadAhead::run(std::shared_ptr<Shared> shared,
                        std::shared_ptr<const storage::AudioReader> reader)
    {
        try
        {
            const auto shape = reader->shape();
            const auto blocks =
                shape.frames / blockFrames + (shape.frames % blockFrames != 0);
            std::array<float, blockFrames> channel;
            while (!shared->closed.load(std::memory_order_acquire))
            {
                const auto wanted =
                    shared->wanted.load(std::memory_order_acquire);
                const auto loop = shared->loop.load(std::memory_order_acquire);
                // Current audio first, then the loop entrance and sequential
                // lead.
                const std::array<int64_t, 6> keys{
                    wanted,     loop,       wanted + 1,
                    wanted + 2, wanted + 3, loop < 0 ? -1 : loop + 1};
                bool didWork = false;
                for (const auto key : keys)
                {
                    if (key < 0 || key >= blocks ||
                        shared->closed.load(std::memory_order_acquire))
                    {
                        continue;
                    }
                    const auto present = std::any_of(
                        shared->slots.begin(), shared->slots.end(),
                        [key](const auto &slot)
                        {
                            const auto state =
                                slot.state.load(std::memory_order_acquire);
                            // Only this worker writes slot.block; consumer
                            // never modifies it.
                            return (state == Ready || state == Reading) &&
                                   slot.block == key;
                        });
                    if (present)
                    {
                        continue;
                    }
                    for (auto &slot : shared->slots)
                    {
                        auto expected =
                            slot.state.load(std::memory_order_acquire);
                        if (expected != Empty && expected != Ready)
                        {
                            continue;
                        }
                        if (expected == Ready &&
                            std::find(keys.begin(), keys.end(), slot.block) !=
                                keys.end())
                        {
                            continue;
                        }
                        if (!slot.state.compare_exchange_strong(
                                expected, Writing, std::memory_order_acquire,
                                std::memory_order_relaxed))
                        {
                            continue;
                        }
                        slot.block = key;
                        const auto count = std::min(
                            blockFrames, shape.frames - key * blockFrames);
                        for (int ch = 0; ch < shape.channels; ++ch)
                        {
                            if (shared->closed.load(std::memory_order_acquire))
                            {
                                break;
                            }
                            reader->readChannel(
                                ch, key * blockFrames,
                                {channel.data(), std::size_t(count)});
                            for (int64_t i = 0; i < count; ++i)
                            {
                                slot.samples[i * 2 + ch] = channel[i];
                            }
                        }
                        if (shape.channels == 1)
                        {
                            for (int64_t i = 0; i < count; ++i)
                            {
                                slot.samples[i * 2 + 1] = slot.samples[i * 2];
                            }
                        }
                        if (shared->closed.load(std::memory_order_acquire))
                        {
                            break;
                        }
                        slot.state.store(Ready, std::memory_order_release);
                        didWork = true;
                        break;
                    }
                    // A seek can arrive during an I/O operation. Replan before
                    // reading another obsolete block; old slots remain safe.
                    if (shared->wanted.load(std::memory_order_acquire) !=
                            wanted ||
                        shared->loop.load(std::memory_order_acquire) != loop)
                    {
                        break;
                    }
                }
                if (!didWork)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        }
        catch (...)
        {
            shared->failed.store(true, std::memory_order_release);
        }
        reader.reset(); // Final source/store release belongs to this worker.
        shared->finished.store(true, std::memory_order_release);
    }
} // namespace cupuacu::playback
