#pragma once

#include "SampleProvenance.hpp"

#include <algorithm>
#include <cstring>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace cupuacu::audio
{
    class AudioBuffer
    {
    protected:
        std::vector<std::vector<float>> channels;

    public:
        using ProgressCallback =
            std::function<void(int64_t completed, int64_t total)>;

        virtual void assignChannels(
            const std::vector<std::vector<float>> &samples,
            const std::vector<std::vector<SampleProvenance>> &provenance,
            bool shouldMarkDirty = false,
            const ProgressCallback &progress = {})
        {
            constexpr int64_t kProgressStrideFrames = 262144;

            (void)provenance;

            const auto writableChannels = std::min<std::size_t>(
                channels.size(), samples.size());
            int64_t totalFrames = 0;
            for (std::size_t channel = 0; channel < writableChannels; ++channel)
            {
                totalFrames += std::min<std::size_t>(
                    channels[channel].size(), samples[channel].size());
            }

            int64_t completedFrames = 0;
            for (std::size_t channel = 0; channel < writableChannels; ++channel)
            {
                auto &destination = channels[channel];
                const auto &source = samples[channel];
                const auto writableFrames = std::min<std::size_t>(
                    destination.size(), source.size());
                for (std::size_t frame = 0; frame < writableFrames;
                     frame += static_cast<std::size_t>(kProgressStrideFrames))
                {
                    const auto chunkFrames = std::min<std::size_t>(
                        writableFrames - frame,
                        static_cast<std::size_t>(kProgressStrideFrames));
                    std::copy_n(source.data() + frame, chunkFrames,
                                destination.data() + frame);
                    completedFrames += static_cast<int64_t>(chunkFrames);
                    if (progress)
                    {
                        progress(completedFrames, std::max<int64_t>(1, totalFrames));
                    }
                }
            }

            (void)shouldMarkDirty;
            if (progress)
            {
                progress(std::max<int64_t>(1, totalFrames),
                         std::max<int64_t>(1, totalFrames));
            }
        }

        virtual bool isDirty(int64_t channel, int64_t frame) const
        {
            return true;
        }

        virtual SampleProvenance getProvenance(int64_t channel,
                                               int64_t frame) const
        {
            return {};
        }

        virtual void setProvenance(int64_t channel, int64_t frame,
                                   const SampleProvenance &sampleProvenance)
        {
        }

        virtual void markAllClean()
        {
        }

        virtual void establishSequentialProvenance(const std::uint64_t sourceId)
        {
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

        virtual void insertFrames(int64_t frameIndex, int64_t numFrames,
                                  const ProgressCallback &progress = {})
        {
            if (numFrames <= 0)
            {
                if (progress)
                {
                    progress(1, 1);
                }
                return;
            }
            const int64_t channelCount =
                static_cast<int64_t>(channels.size());
            int64_t completedChannels = 0;
            for (auto &ch : channels)
            {
                if (frameIndex == static_cast<int64_t>(ch.size()))
                {
                    const int64_t newSize = static_cast<int64_t>(ch.size()) + numFrames;
                    if (newSize > static_cast<int64_t>(ch.capacity()))
                    {
                        const int64_t grown =
                            std::max<int64_t>(newSize, static_cast<int64_t>(ch.capacity()) * 2);
                        ch.reserve(grown);
                    }
                    ch.insert(ch.end(), numFrames, 0.0f);
                }
                else
                {
                    ch.insert(ch.begin() + frameIndex, numFrames, 0.0f);
                }
                ++completedChannels;
                if (progress)
                {
                    progress(completedChannels,
                             std::max<int64_t>(1, channelCount));
                }
            }

            if (progress && completedChannels == 0)
            {
                progress(1, 1);
            }
        }

        virtual void removeFrames(int64_t frameIndex, int64_t numFrames,
                                  const ProgressCallback &progress = {})
        {
            if (numFrames <= 0)
            {
                if (progress)
                {
                    progress(1, 1);
                }
                return;
            }

            constexpr int64_t kProgressStrideFrames = 16384;

            int64_t totalFramesToShift = 0;
            for (const auto &ch : channels)
            {
                const int64_t oldSize = static_cast<int64_t>(ch.size());
                const int64_t newSize = oldSize - numFrames;
                totalFramesToShift += std::max<int64_t>(1, newSize - frameIndex);
            }

            int64_t completedFrames = 0;
            for (auto &ch : channels)
            {
                const int64_t oldSize = static_cast<int64_t>(ch.size());
                const int64_t newSize = oldSize - numFrames;
                const int64_t framesToShift = newSize - frameIndex;

                if (framesToShift > 0)
                {
                    for (int64_t movedFrames = 0; movedFrames < framesToShift;
                         movedFrames += kProgressStrideFrames)
                    {
                        const int64_t chunkFrames = std::min<int64_t>(
                            kProgressStrideFrames, framesToShift - movedFrames);
                        std::memmove(ch.data() + frameIndex + movedFrames,
                                     ch.data() + frameIndex + numFrames + movedFrames,
                                     static_cast<std::size_t>(chunkFrames) *
                                         sizeof(float));
                        completedFrames += chunkFrames;
                        if (progress)
                        {
                            progress(completedFrames,
                                     std::max<int64_t>(1, totalFramesToShift));
                        }
                    }
                }
                else
                {
                    completedFrames += 1;
                    if (progress)
                    {
                        progress(completedFrames,
                                 std::max<int64_t>(1, totalFramesToShift));
                    }
                }

                ch.resize(static_cast<std::size_t>(newSize));
            }

            if (progress)
            {
                progress(std::max<int64_t>(1, totalFramesToShift),
                         std::max<int64_t>(1, totalFramesToShift));
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
            if (channel < 0 || channel >= static_cast<int64_t>(channels.size()))
            {
                return {};
            }
            return channels[channel];
        }

        std::span<float> getMutableChannelData(int64_t channel)
        {
            if (channel < 0 || channel >= static_cast<int64_t>(channels.size()))
            {
                return {};
            }
            return channels[channel];
        }
    };
} // namespace cupuacu::audio
