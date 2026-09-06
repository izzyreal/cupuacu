#pragma once
#include "AudioBlockStore.hpp"
#include <shared_mutex>
namespace cupuacu::storage
{
    // Only sealed blocks are published. Reads belong to range/read-ahead
    // workers; the UI observes availability atomically and never fetches sample
    // bytes.
    class ImportAudioReader final : public AudioReader
    {
        AudioShape dimensions;
        std::shared_ptr<AudioBlockStore> store;
        std::shared_ptr<DecodedBlockCache> cache;
        mutable std::shared_mutex mutex;
        std::vector<std::vector<AudioBlock>> channels;
        std::atomic<int64_t> available{0};

    public:
        ImportAudioReader(AudioShape shape,
                          std::shared_ptr<AudioBlockStore> store,
                          std::shared_ptr<DecodedBlockCache> cache)
            : dimensions(shape), store(std::move(store)),
              cache(std::move(cache)), channels(shape.channels)
        {
        }
        AudioShape shape() const override
        {
            return dimensions;
        }
        int64_t availableFrames() const
        {
            return available.load(std::memory_order_acquire);
        }
        void publish(std::span<const AudioBlock> blocks)
        {
            if (blocks.size() != channels.size() || blocks.empty())
            {
                throw std::logic_error("Invalid imported block publication");
            }
            store->flush();
            std::unique_lock lock(mutex);
            for (std::size_t c = 0; c < blocks.size(); ++c)
            {
                channels[c].push_back(blocks[c]);
            }
            available.fetch_add(blocks[0].frames, std::memory_order_release);
        }
        void readChannel(int channel, int64_t first,
                         std::span<float> output) const override
        {
            auto readable = dimensions;
            readable.frames = availableFrames();
            validateRange(readable, channel, first, output.size());
            while (!output.empty())
            {
                AudioBlock block;
                {
                    std::shared_lock lock(mutex);
                    block = channels[channel][first / AudioBlockFrames];
                }
                const auto offset = uint32_t(first % AudioBlockFrames);
                const auto count =
                    std::min<std::size_t>(block.frames - offset, output.size());
                cache->read(*store, block, offset, output.first(count));
                first += count;
                output = output.subspan(count);
            }
        }
    };
} // namespace cupuacu::storage
