#pragma once

#include <vector>
#include <span>

namespace cupuacu
{
    class AudioBuffer
    {
    protected:
        std::vector<std::vector<float>> channels;

    public:
        virtual bool isDirty(int64_t channel, int64_t frame) const
        {
            return true;
        }

        virtual void resize(int64_t numChannels, int64_t numFrames)
        {
            channels.resize(numChannels);
            for (auto &ch : channels)
            {
                ch.resize(numFrames);
            }
        }

        virtual void setSample(int64_t channel, int64_t frame, float value,
                               const bool shouldMarkDirty = true)
        {
            channels[channel][frame] = value;
        }

        virtual void insertFrames(int64_t frameIndex, int64_t numFrames)
        {
            int64_t chCount = channels.size();
            for (auto &ch : channels)
            {
                ch.insert(ch.begin() + frameIndex, numFrames, 0.0f);
            }
        }

        virtual void removeFrames(int64_t frameIndex, int64_t numFrames)
        {
            int64_t chCount = channels.size();
            for (auto &ch : channels)
            {
                ch.erase(ch.begin() + frameIndex,
                         ch.begin() + frameIndex + numFrames);
            }
        }

        int64_t getFrameCount() const
        {
            if (channels.empty())
            {
                return 0;
            }
            return channels[0].size();
        }

        int64_t getChannelCount() const
        {
            return channels.size();
        }

        float getSample(int64_t channel, int64_t frame) const
        {
            return channels[channel][frame];
        }

        std::span<const float> getImmutableChannelData(int64_t channel) const
        {
            return channels[channel];
        }
    };
} // namespace cupuacu
