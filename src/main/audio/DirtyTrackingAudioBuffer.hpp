#pragma once

#include "audio/AudioBuffer.hpp"
#include <vector>
#include <cstdint>

namespace cupuacu::audio
{

    class DirtyTrackingAudioBuffer : public AudioBuffer
    {
    private:
        std::vector<uint8_t> dirtyFlags;

        bool isDirty(int64_t channel, int64_t frame) const override
        {
            int64_t idx = frame * channels.size() + channel;
            return (dirtyFlags[idx / 8] >> (idx % 8)) & 1;
        }

        void markDirty(int64_t channel, int64_t frame)
        {
            int64_t idx = frame * channels.size() + channel;
            dirtyFlags[idx / 8] |= (1 << (idx % 8));
        }

        void clearDirty(int64_t channel, int64_t frame)
        {
            int64_t idx = frame * channels.size() + channel;
            dirtyFlags[idx / 8] &= ~(1 << (idx % 8));
        }

    public:
        void resize(int64_t numChannels, int64_t numFrames) override
        {
            AudioBuffer::resize(numChannels, numFrames);
            dirtyFlags.resize((numChannels * numFrames + 7) / 8, 0);
        }

        void setSample(int64_t channel, int64_t frame, float value,
                       const bool shouldMarkDirty = true) override
        {
            AudioBuffer::setSample(channel, frame, value);

            if (shouldMarkDirty)
            {
                markDirty(channel, frame);
            }
        }

        void insertFrames(int64_t frameIndex, int64_t numFrames) override
        {
            const int64_t oldFrameCount = getFrameCount();
            const int64_t chCount = getChannelCount();
            AudioBuffer::insertFrames(frameIndex, numFrames);
            const int64_t newFrameCount = getFrameCount();
            const int64_t oldBitCount = oldFrameCount * chCount;
            const int64_t newBitCount = newFrameCount * chCount;
            if (frameIndex >= oldFrameCount)
            {
                // Appending recording data should keep existing dirty flags and
                // add clean flags for newly appended frames.
                const int64_t newBytes = (newBitCount + 7) / 8;
                if (newBytes > static_cast<int64_t>(dirtyFlags.capacity()))
                {
                    const int64_t grown = std::max<int64_t>(
                        newBytes, static_cast<int64_t>(dirtyFlags.capacity()) * 2);
                    dirtyFlags.reserve(grown);
                }
                dirtyFlags.resize(newBytes, 0);
                return;
            }

            std::vector<uint8_t> oldFlags = dirtyFlags;
            oldFlags.resize((oldBitCount + 7) / 8, 0);
            dirtyFlags = std::move(oldFlags);
            std::vector<uint8_t> newFlags((newBitCount + 7) / 8, 0);

            for (int64_t f = 0; f < frameIndex; ++f)
            {
                for (int64_t c = 0; c < chCount; ++c)
                {
                    if (isDirty(c, f))
                    {
                        int64_t idx = f * chCount + c;
                        newFlags[idx / 8] |= (1 << (idx % 8));
                    }
                }
            }

            for (int64_t f = frameIndex; f < newFrameCount - numFrames; ++f)
            {
                for (int64_t c = 0; c < chCount; ++c)
                {
                    if (isDirty(c, f))
                    {
                        int64_t newIdx = (f + numFrames) * chCount + c;
                        newFlags[newIdx / 8] |= (1 << (newIdx % 8));
                    }
                }
            }

            dirtyFlags = std::move(newFlags);
        }

        void removeFrames(int64_t frameIndex, int64_t numFrames) override
        {
            AudioBuffer::removeFrames(frameIndex, numFrames);

            auto chCount = getChannelCount();

            std::vector<uint8_t> newFlags((chCount * getFrameCount() + 7) / 8,
                                          0);

            for (int64_t f = 0; f < frameIndex; ++f)
            {
                for (int64_t c = 0; c < chCount; ++c)
                {
                    if (isDirty(c, f))
                    {
                        int64_t idx = f * chCount + c;
                        newFlags[idx / 8] |= (1 << (idx % 8));
                    }
                }
            }

            for (int64_t f = frameIndex; f < getFrameCount(); ++f)
            {
                for (int64_t c = 0; c < chCount; ++c)
                {
                    if (isDirty(c, f + numFrames))
                    {
                        int64_t newIdx = f * chCount + c;
                        newFlags[newIdx / 8] |= (1 << (newIdx % 8));
                    }
                }
            }

            dirtyFlags = std::move(newFlags);
        }
    };
} // namespace cupuacu::audio
