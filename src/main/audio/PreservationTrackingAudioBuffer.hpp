#pragma once

#include "AudioBuffer.hpp"
#include "SampleProvenance.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace cupuacu::audio
{
    class PreservationTrackingAudioBuffer : public AudioBuffer
    {
    private:
        std::vector<std::uint8_t> dirtyFlags;
        std::vector<SampleProvenance> provenance;

        [[nodiscard]] std::int64_t flatIndex(const std::int64_t channel,
                                             const std::int64_t frame) const
        {
            return frame * static_cast<std::int64_t>(channels.size()) + channel;
        }

        [[nodiscard]] bool dirtyBitByFlatIndex(const std::int64_t index) const
        {
            return (dirtyFlags[static_cast<std::size_t>(index / 8)] >>
                    (index % 8)) &
                   1;
        }

        void markDirtyByFlatIndex(const std::int64_t index)
        {
            dirtyFlags[static_cast<std::size_t>(index / 8)] |=
                (1u << (index % 8));
        }

    public:
        [[nodiscard]] bool isDirty(const std::int64_t channel,
                                   const std::int64_t frame) const override
        {
            return dirtyBitByFlatIndex(flatIndex(channel, frame));
        }

        void resize(const std::int64_t numChannels,
                    const std::int64_t numFrames) override
        {
            AudioBuffer::resize(numChannels, numFrames);
            const auto sampleCount = numChannels * numFrames;
            dirtyFlags.assign(static_cast<std::size_t>((sampleCount + 7) / 8), 0);
            provenance.assign(static_cast<std::size_t>(sampleCount), {});
        }

        void setSample(const std::int64_t channel, const std::int64_t frame,
                       const float value,
                       const bool shouldMarkDirty = true) override
        {
            AudioBuffer::setSample(channel, frame, value, shouldMarkDirty);
            if (shouldMarkDirty)
            {
                markDirtyByFlatIndex(flatIndex(channel, frame));
            }
        }

        void insertFrames(const std::int64_t frameIndex,
                          const std::int64_t numFrames) override
        {
            const auto oldFrameCount = getFrameCount();
            const auto channelCount = getChannelCount();
            const auto oldSampleCount = oldFrameCount * channelCount;

            std::vector<std::uint8_t> oldDirtyFlags = dirtyFlags;
            std::vector<SampleProvenance> oldProvenance = provenance;
            oldDirtyFlags.resize(static_cast<std::size_t>((oldSampleCount + 7) / 8),
                                 0);
            oldProvenance.resize(static_cast<std::size_t>(oldSampleCount));

            AudioBuffer::insertFrames(frameIndex, numFrames);

            const auto newFrameCount = getFrameCount();
            const auto newSampleCount = newFrameCount * channelCount;
            dirtyFlags.assign(static_cast<std::size_t>((newSampleCount + 7) / 8), 0);
            provenance.assign(static_cast<std::size_t>(newSampleCount), {});

            const auto oldIsDirty = [&](const std::int64_t channel,
                                        const std::int64_t frame) -> bool
            {
                const auto index = frame * channelCount + channel;
                return (oldDirtyFlags[static_cast<std::size_t>(index / 8)] >>
                        (index % 8)) &
                       1;
            };

            for (std::int64_t frame = 0; frame < frameIndex; ++frame)
            {
                for (std::int64_t channel = 0; channel < channelCount; ++channel)
                {
                    const auto index = frame * channelCount + channel;
                    if (oldIsDirty(channel, frame))
                    {
                        markDirtyByFlatIndex(index);
                    }
                    provenance[static_cast<std::size_t>(index)] =
                        oldProvenance[static_cast<std::size_t>(index)];
                }
            }

            for (std::int64_t frame = frameIndex; frame < oldFrameCount; ++frame)
            {
                for (std::int64_t channel = 0; channel < channelCount; ++channel)
                {
                    const auto oldIndex = frame * channelCount + channel;
                    const auto newIndex =
                        (frame + numFrames) * channelCount + channel;
                    if (oldIsDirty(channel, frame))
                    {
                        markDirtyByFlatIndex(newIndex);
                    }
                    provenance[static_cast<std::size_t>(newIndex)] =
                        oldProvenance[static_cast<std::size_t>(oldIndex)];
                }
            }
        }

        void removeFrames(const std::int64_t frameIndex,
                          const std::int64_t numFrames) override
        {
            const auto oldFrameCount = getFrameCount();
            const auto channelCount = getChannelCount();
            const auto oldSampleCount = oldFrameCount * channelCount;

            std::vector<std::uint8_t> oldDirtyFlags = dirtyFlags;
            std::vector<SampleProvenance> oldProvenance = provenance;
            oldDirtyFlags.resize(static_cast<std::size_t>((oldSampleCount + 7) / 8),
                                 0);
            oldProvenance.resize(static_cast<std::size_t>(oldSampleCount));

            AudioBuffer::removeFrames(frameIndex, numFrames);

            const auto newFrameCount = getFrameCount();
            const auto newSampleCount = newFrameCount * channelCount;
            dirtyFlags.assign(static_cast<std::size_t>((newSampleCount + 7) / 8), 0);
            provenance.assign(static_cast<std::size_t>(newSampleCount), {});

            const auto oldIsDirty = [&](const std::int64_t channel,
                                        const std::int64_t frame) -> bool
            {
                const auto index = frame * channelCount + channel;
                return (oldDirtyFlags[static_cast<std::size_t>(index / 8)] >>
                        (index % 8)) &
                       1;
            };

            for (std::int64_t frame = 0; frame < frameIndex; ++frame)
            {
                for (std::int64_t channel = 0; channel < channelCount; ++channel)
                {
                    const auto index = frame * channelCount + channel;
                    if (oldIsDirty(channel, frame))
                    {
                        markDirtyByFlatIndex(index);
                    }
                    provenance[static_cast<std::size_t>(index)] =
                        oldProvenance[static_cast<std::size_t>(index)];
                }
            }

            for (std::int64_t frame = frameIndex; frame < newFrameCount; ++frame)
            {
                for (std::int64_t channel = 0; channel < channelCount; ++channel)
                {
                    const auto oldIndex =
                        (frame + numFrames) * channelCount + channel;
                    const auto newIndex = frame * channelCount + channel;
                    if (oldIsDirty(channel, frame + numFrames))
                    {
                        markDirtyByFlatIndex(newIndex);
                    }
                    provenance[static_cast<std::size_t>(newIndex)] =
                        oldProvenance[static_cast<std::size_t>(oldIndex)];
                }
            }
        }

        [[nodiscard]] SampleProvenance
        getProvenance(const std::int64_t channel,
                      const std::int64_t frame) const override
        {
            return provenance[static_cast<std::size_t>(flatIndex(channel, frame))];
        }

        void setProvenance(const std::int64_t channel, const std::int64_t frame,
                           const SampleProvenance &sampleProvenance) override
        {
            provenance[static_cast<std::size_t>(flatIndex(channel, frame))] =
                sampleProvenance;
        }

        void markAllClean() override
        {
            std::fill(dirtyFlags.begin(), dirtyFlags.end(), 0);
        }

        void establishSequentialProvenance(const std::uint64_t sourceId) override
        {
            const auto channelCount = getChannelCount();
            const auto frameCount = getFrameCount();
            for (std::int64_t frame = 0; frame < frameCount; ++frame)
            {
                for (std::int64_t channel = 0; channel < channelCount; ++channel)
                {
                    setProvenance(channel, frame,
                                  SampleProvenance{sourceId, frame});
                }
            }
            markAllClean();
        }
    };
} // namespace cupuacu::audio
