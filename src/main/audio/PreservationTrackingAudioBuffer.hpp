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
        struct ProvenanceRange
        {
            std::int64_t startFrame = 0;
            std::int64_t endFrameExclusive = 0;
            std::uint64_t sourceId = 0;
            std::int64_t sourceStartFrame = -1;
        };

        std::vector<std::uint8_t> dirtyFlags;
        std::vector<std::vector<ProvenanceRange>> provenanceRanges;

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

        [[nodiscard]] static bool isValidProvenance(
            const SampleProvenance &sampleProvenance)
        {
            return sampleProvenance.sourceId != 0 &&
                   sampleProvenance.frameIndex >= 0;
        }

        static void mergeAdjacentRanges(
            std::vector<ProvenanceRange> &ranges)
        {
            if (ranges.empty())
            {
                return;
            }

            std::sort(ranges.begin(), ranges.end(),
                      [](const ProvenanceRange &left,
                         const ProvenanceRange &right)
                      { return left.startFrame < right.startFrame; });

            std::vector<ProvenanceRange> merged;
            merged.reserve(ranges.size());
            merged.push_back(ranges.front());
            for (std::size_t i = 1; i < ranges.size(); ++i)
            {
                auto &tail = merged.back();
                const auto &current = ranges[i];
                const bool canMerge =
                    tail.endFrameExclusive == current.startFrame &&
                    tail.sourceId == current.sourceId &&
                    tail.sourceStartFrame +
                            (tail.endFrameExclusive - tail.startFrame) ==
                        current.sourceStartFrame;
                if (canMerge)
                {
                    tail.endFrameExclusive = current.endFrameExclusive;
                    continue;
                }
                merged.push_back(current);
            }
            ranges = std::move(merged);
        }

        void assignProvenanceRange(const std::int64_t channel,
                                   const std::int64_t startFrame,
                                   const std::int64_t endFrameExclusive,
                                   const SampleProvenance &sampleProvenance)
        {
            if (channel < 0 ||
                channel >= static_cast<std::int64_t>(provenanceRanges.size()) ||
                startFrame >= endFrameExclusive)
            {
                return;
            }

            auto &ranges = provenanceRanges[static_cast<std::size_t>(channel)];
            std::vector<ProvenanceRange> updated;
            updated.reserve(ranges.size() + 1);
            for (const auto &range : ranges)
            {
                if (range.endFrameExclusive <= startFrame ||
                    range.startFrame >= endFrameExclusive)
                {
                    updated.push_back(range);
                    continue;
                }

                if (range.startFrame < startFrame)
                {
                    updated.push_back(
                        {.startFrame = range.startFrame,
                         .endFrameExclusive = startFrame,
                         .sourceId = range.sourceId,
                         .sourceStartFrame = range.sourceStartFrame});
                }

                if (range.endFrameExclusive > endFrameExclusive)
                {
                    updated.push_back(
                        {.startFrame = endFrameExclusive,
                         .endFrameExclusive = range.endFrameExclusive,
                         .sourceId = range.sourceId,
                         .sourceStartFrame =
                             range.sourceStartFrame +
                             (endFrameExclusive - range.startFrame)});
                }
            }

            if (isValidProvenance(sampleProvenance))
            {
                updated.push_back(
                    {.startFrame = startFrame,
                     .endFrameExclusive = endFrameExclusive,
                     .sourceId = sampleProvenance.sourceId,
                     .sourceStartFrame = sampleProvenance.frameIndex});
            }

            mergeAdjacentRanges(updated);
            ranges = std::move(updated);
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
            provenanceRanges.assign(static_cast<std::size_t>(numChannels), {});
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
            if (numFrames <= 0)
            {
                return;
            }

            const auto oldFrameCount = getFrameCount();
            const auto channelCount = getChannelCount();
            const auto oldSampleCount = oldFrameCount * channelCount;

            std::vector<std::uint8_t> oldDirtyFlags = dirtyFlags;
            oldDirtyFlags.resize(static_cast<std::size_t>((oldSampleCount + 7) / 8),
                                 0);

            AudioBuffer::insertFrames(frameIndex, numFrames);

            const auto newFrameCount = getFrameCount();
            const auto newSampleCount = newFrameCount * channelCount;
            dirtyFlags.assign(static_cast<std::size_t>((newSampleCount + 7) / 8), 0);

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
                }
            }

            for (std::int64_t frame = frameIndex; frame < oldFrameCount; ++frame)
            {
                for (std::int64_t channel = 0; channel < channelCount; ++channel)
                {
                    const auto newIndex =
                        (frame + numFrames) * channelCount + channel;
                    if (oldIsDirty(channel, frame))
                    {
                        markDirtyByFlatIndex(newIndex);
                    }
                }
            }

            for (std::int64_t channel = 0; channel < channelCount; ++channel)
            {
                auto &ranges =
                    provenanceRanges[static_cast<std::size_t>(channel)];
                std::vector<ProvenanceRange> updated;
                updated.reserve(ranges.size() + 1);
                for (const auto &range : ranges)
                {
                    if (range.endFrameExclusive <= frameIndex)
                    {
                        updated.push_back(range);
                        continue;
                    }
                    if (range.startFrame >= frameIndex)
                    {
                        updated.push_back(
                            {.startFrame = range.startFrame + numFrames,
                             .endFrameExclusive =
                                 range.endFrameExclusive + numFrames,
                             .sourceId = range.sourceId,
                             .sourceStartFrame = range.sourceStartFrame});
                        continue;
                    }

                    updated.push_back(
                        {.startFrame = range.startFrame,
                         .endFrameExclusive = frameIndex,
                         .sourceId = range.sourceId,
                         .sourceStartFrame = range.sourceStartFrame});
                    updated.push_back(
                        {.startFrame = frameIndex + numFrames,
                         .endFrameExclusive =
                             range.endFrameExclusive + numFrames,
                         .sourceId = range.sourceId,
                         .sourceStartFrame =
                             range.sourceStartFrame +
                             (frameIndex - range.startFrame)});
                }
                mergeAdjacentRanges(updated);
                ranges = std::move(updated);
            }
        }

        void removeFrames(const std::int64_t frameIndex,
                          const std::int64_t numFrames) override
        {
            if (numFrames <= 0)
            {
                return;
            }

            const auto oldFrameCount = getFrameCount();
            const auto channelCount = getChannelCount();
            const auto oldSampleCount = oldFrameCount * channelCount;

            std::vector<std::uint8_t> oldDirtyFlags = dirtyFlags;
            oldDirtyFlags.resize(static_cast<std::size_t>((oldSampleCount + 7) / 8),
                                 0);

            AudioBuffer::removeFrames(frameIndex, numFrames);

            const auto newFrameCount = getFrameCount();
            const auto newSampleCount = newFrameCount * channelCount;
            dirtyFlags.assign(static_cast<std::size_t>((newSampleCount + 7) / 8), 0);

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
                }
            }

            for (std::int64_t frame = frameIndex; frame < newFrameCount; ++frame)
            {
                for (std::int64_t channel = 0; channel < channelCount; ++channel)
                {
                    const auto newIndex = frame * channelCount + channel;
                    if (oldIsDirty(channel, frame + numFrames))
                    {
                        markDirtyByFlatIndex(newIndex);
                    }
                }
            }

            const auto removeEnd = frameIndex + numFrames;
            for (std::int64_t channel = 0; channel < channelCount; ++channel)
            {
                auto &ranges =
                    provenanceRanges[static_cast<std::size_t>(channel)];
                std::vector<ProvenanceRange> updated;
                updated.reserve(ranges.size());
                for (const auto &range : ranges)
                {
                    if (range.endFrameExclusive <= frameIndex)
                    {
                        updated.push_back(range);
                        continue;
                    }
                    if (range.startFrame >= removeEnd)
                    {
                        updated.push_back(
                            {.startFrame = range.startFrame - numFrames,
                             .endFrameExclusive =
                                 range.endFrameExclusive - numFrames,
                             .sourceId = range.sourceId,
                             .sourceStartFrame = range.sourceStartFrame});
                        continue;
                    }

                    if (range.startFrame < frameIndex)
                    {
                        updated.push_back(
                            {.startFrame = range.startFrame,
                             .endFrameExclusive = frameIndex,
                             .sourceId = range.sourceId,
                             .sourceStartFrame = range.sourceStartFrame});
                    }
                    if (range.endFrameExclusive > removeEnd)
                    {
                        updated.push_back(
                            {.startFrame = frameIndex,
                             .endFrameExclusive =
                                 range.endFrameExclusive - numFrames,
                             .sourceId = range.sourceId,
                             .sourceStartFrame =
                                 range.sourceStartFrame +
                                 (removeEnd - range.startFrame)});
                    }
                }
                mergeAdjacentRanges(updated);
                ranges = std::move(updated);
            }
        }

        [[nodiscard]] SampleProvenance
        getProvenance(const std::int64_t channel,
                      const std::int64_t frame) const override
        {
            if (channel < 0 ||
                channel >= static_cast<std::int64_t>(provenanceRanges.size()) ||
                frame < 0)
            {
                return {};
            }

            const auto &ranges =
                provenanceRanges[static_cast<std::size_t>(channel)];
            auto it = std::lower_bound(
                ranges.begin(), ranges.end(), frame,
                [](const ProvenanceRange &range, const std::int64_t targetFrame)
                { return range.endFrameExclusive <= targetFrame; });
            if (it == ranges.end() || frame < it->startFrame ||
                frame >= it->endFrameExclusive)
            {
                return {};
            }
            return {.sourceId = it->sourceId,
                    .frameIndex =
                        it->sourceStartFrame + (frame - it->startFrame)};
        }

        void setProvenance(const std::int64_t channel, const std::int64_t frame,
                           const SampleProvenance &sampleProvenance) override
        {
            assignProvenanceRange(channel, frame, frame + 1, sampleProvenance);
        }

        void markAllClean() override
        {
            std::fill(dirtyFlags.begin(), dirtyFlags.end(), 0);
        }

        void establishSequentialProvenance(const std::uint64_t sourceId) override
        {
            const auto channelCount = getChannelCount();
            const auto frameCount = getFrameCount();
            provenanceRanges.assign(static_cast<std::size_t>(channelCount), {});
            for (std::int64_t channel = 0; channel < channelCount; ++channel)
            {
                if (frameCount > 0)
                {
                    provenanceRanges[static_cast<std::size_t>(channel)].push_back(
                        {.startFrame = 0,
                         .endFrameExclusive = frameCount,
                         .sourceId = sourceId,
                         .sourceStartFrame = 0});
                }
            }
            markAllClean();
        }
    };
} // namespace cupuacu::audio
