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
        void assignChannels(
            const std::vector<std::vector<float>> &samples,
            const std::vector<std::vector<SampleProvenance>> &provenance,
            const bool shouldMarkDirty = false,
            const AudioBuffer::ProgressCallback &progress = {}) override
        {
            constexpr std::int64_t kProgressStrideFrames = 262144;

            const auto writableChannels = std::min<std::size_t>(
                channels.size(), samples.size());
            std::int64_t totalSampleFrames = 0;
            std::int64_t totalProvenanceFrames = 0;
            for (std::size_t channel = 0; channel < writableChannels; ++channel)
            {
                totalSampleFrames += static_cast<std::int64_t>(std::min<std::size_t>(
                    channels[channel].size(), samples[channel].size()));
                if (channel < provenance.size())
                {
                    totalProvenanceFrames += static_cast<std::int64_t>(
                        std::min<std::size_t>(channels[channel].size(),
                                              provenance[channel].size()));
                }
            }
            const auto totalUnits =
                std::max<std::int64_t>(1, totalSampleFrames + totalProvenanceFrames);

            std::int64_t completedSampleFrames = 0;
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
                    completedSampleFrames += static_cast<std::int64_t>(chunkFrames);
                    if (progress)
                    {
                        progress(completedSampleFrames, totalUnits);
                    }
                }
            }

            const auto channelCount = getChannelCount();
            const auto frameCount = getFrameCount();
            const auto sampleCount = channelCount * frameCount;
            const auto dirtyFillValue =
                static_cast<std::uint8_t>(shouldMarkDirty ? 0xFF : 0x00);
            dirtyFlags.assign(static_cast<std::size_t>((sampleCount + 7) / 8),
                              dirtyFillValue);
            if (shouldMarkDirty && sampleCount > 0 && sampleCount % 8 != 0)
            {
                dirtyFlags.back() &=
                    static_cast<std::uint8_t>((1u << (sampleCount % 8)) - 1u);
            }

            provenanceRanges.assign(static_cast<std::size_t>(channelCount), {});
            std::int64_t completedProvenanceFrames = 0;
            for (std::size_t channel = 0; channel < writableChannels; ++channel)
            {
                auto &ranges = provenanceRanges[channel];
                if (channel >= provenance.size())
                {
                    continue;
                }

                const auto writableFrames = std::min<std::size_t>(
                    channels[channel].size(), provenance[channel].size());
                if (writableFrames == 0)
                {
                    continue;
                }

                bool hasActiveRange = false;
                ProvenanceRange activeRange{};
                for (std::size_t frame = 0; frame < writableFrames; ++frame)
                {
                    const auto &sampleProvenance = provenance[channel][frame];
                    const bool canExtendActiveRange =
                        hasActiveRange && sampleProvenance.isValid() &&
                        activeRange.sourceId == sampleProvenance.sourceId &&
                        activeRange.sourceStartFrame +
                                (static_cast<std::int64_t>(frame) -
                                 activeRange.startFrame) ==
                            sampleProvenance.frameIndex;

                    if (canExtendActiveRange)
                    {
                        activeRange.endFrameExclusive =
                            static_cast<std::int64_t>(frame) + 1;
                    }
                    else
                    {
                        if (hasActiveRange)
                        {
                            ranges.push_back(activeRange);
                        }
                        hasActiveRange = false;
                        if (sampleProvenance.isValid())
                        {
                            activeRange = {
                                .startFrame = static_cast<std::int64_t>(frame),
                                .endFrameExclusive =
                                    static_cast<std::int64_t>(frame) + 1,
                                .sourceId = sampleProvenance.sourceId,
                                .sourceStartFrame =
                                    sampleProvenance.frameIndex,
                            };
                            hasActiveRange = true;
                        }
                    }

                    ++completedProvenanceFrames;
                    if (progress &&
                        (((frame + 1) %
                          static_cast<std::size_t>(kProgressStrideFrames)) == 0 ||
                         frame + 1 == writableFrames))
                    {
                        progress(completedSampleFrames + completedProvenanceFrames,
                                 totalUnits);
                    }
                }

                if (hasActiveRange)
                {
                    ranges.push_back(activeRange);
                }
            }

            if (progress)
            {
                progress(totalUnits, totalUnits);
            }
        }

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

        void insertFrames(
            const std::int64_t frameIndex, const std::int64_t numFrames,
            const AudioBuffer::ProgressCallback &progress = {}) override
        {
            if (numFrames <= 0)
            {
                if (progress)
                {
                    progress(1, 1);
                }
                return;
            }

            const auto oldFrameCount = getFrameCount();
            const auto channelCount = getChannelCount();
            const auto oldSampleCount = oldFrameCount * channelCount;

            std::vector<std::uint8_t> oldDirtyFlags = dirtyFlags;
            oldDirtyFlags.resize(static_cast<std::size_t>((oldSampleCount + 7) / 8),
                                 0);

            std::int64_t totalRangeCount = 0;
            for (const auto &ranges : provenanceRanges)
            {
                totalRangeCount += static_cast<std::int64_t>(ranges.size());
            }
            const std::int64_t phase1Units = std::max<std::int64_t>(1, channelCount);
            const std::int64_t phase2Units =
                std::max<std::int64_t>(1, oldFrameCount * channelCount);
            const std::int64_t phase3Units =
                std::max<std::int64_t>(1, totalRangeCount);
            const std::int64_t totalUnits =
                phase1Units + phase2Units + phase3Units;
            const auto publishProgress =
                [&](const std::int64_t completedUnits)
            {
                if (progress)
                {
                    progress(std::clamp<std::int64_t>(completedUnits, 0, totalUnits),
                             totalUnits);
                }
            };

            AudioBuffer::insertFrames(
                frameIndex, numFrames,
                [&](const std::int64_t completed, const std::int64_t total)
                {
                    const auto safeTotal = std::max<std::int64_t>(1, total);
                    publishProgress(completed * phase1Units / safeTotal);
                });

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

            constexpr std::int64_t kProgressStrideFrames = 16384;
            std::int64_t dirtyUnitsCompleted = 0;
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
                dirtyUnitsCompleted += channelCount;
                if ((frame + 1) % kProgressStrideFrames == 0 ||
                    frame + 1 == frameIndex)
                {
                    publishProgress(phase1Units +
                                    dirtyUnitsCompleted * phase2Units /
                                        std::max<std::int64_t>(1, oldFrameCount * channelCount));
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
                dirtyUnitsCompleted += channelCount;
                if ((frame - frameIndex + 1) % kProgressStrideFrames == 0 ||
                    frame + 1 == oldFrameCount)
                {
                    publishProgress(phase1Units +
                                    dirtyUnitsCompleted * phase2Units /
                                        std::max<std::int64_t>(1, oldFrameCount * channelCount));
                }
            }

            std::int64_t provenanceRangesCompleted = 0;
            for (std::int64_t channel = 0; channel < channelCount; ++channel)
            {
                auto &ranges =
                    provenanceRanges[static_cast<std::size_t>(channel)];
                const auto originalRangeCount =
                    static_cast<std::int64_t>(ranges.size());
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
                provenanceRangesCompleted += originalRangeCount;
                publishProgress(phase1Units + phase2Units +
                                provenanceRangesCompleted * phase3Units /
                                    std::max<std::int64_t>(1, totalRangeCount));
            }

            publishProgress(totalUnits);
        }

        void removeFrames(
            const std::int64_t frameIndex, const std::int64_t numFrames,
            const AudioBuffer::ProgressCallback &progress = {}) override
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

            const std::int64_t phase1Units = std::max<std::int64_t>(1, channelCount);

            AudioBuffer::removeFrames(
                frameIndex, numFrames,
                [&](const std::int64_t completed, const std::int64_t total)
                {
                    const auto safeTotal = std::max<std::int64_t>(1, total);
                    if (progress)
                    {
                        progress(completed * phase1Units / safeTotal, phase1Units);
                    }
                });

            const auto newFrameCount = getFrameCount();
            const auto newSampleCount = newFrameCount * channelCount;
            dirtyFlags.assign(static_cast<std::size_t>((newSampleCount + 7) / 8), 0);
            const std::int64_t phase2Units =
                std::max<std::int64_t>(1, newFrameCount * channelCount);
            std::int64_t totalRangeCount = 0;
            for (const auto &ranges : provenanceRanges)
            {
                totalRangeCount += static_cast<std::int64_t>(ranges.size());
            }
            const std::int64_t phase3Units =
                std::max<std::int64_t>(1, totalRangeCount);
            const std::int64_t totalUnits =
                phase1Units + phase2Units + phase3Units;
            const auto publishProgress =
                [&](const std::int64_t completedUnits)
            {
                if (progress)
                {
                    progress(std::clamp<std::int64_t>(completedUnits, 0, totalUnits),
                             totalUnits);
                }
            };

            const auto oldIsDirty = [&](const std::int64_t channel,
                                        const std::int64_t frame) -> bool
            {
                const auto index = frame * channelCount + channel;
                return (oldDirtyFlags[static_cast<std::size_t>(index / 8)] >>
                        (index % 8)) &
                       1;
            };

            constexpr std::int64_t kProgressStrideFrames = 16384;
            std::int64_t dirtyUnitsCompleted = 0;
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
                dirtyUnitsCompleted += channelCount;
                if ((frame + 1) % kProgressStrideFrames == 0 ||
                    frame + 1 == frameIndex)
                {
                    publishProgress(phase1Units +
                                    dirtyUnitsCompleted * phase2Units /
                                        std::max<std::int64_t>(1, newFrameCount * channelCount));
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
                dirtyUnitsCompleted += channelCount;
                if ((frame - frameIndex + 1) % kProgressStrideFrames == 0 ||
                    frame + 1 == newFrameCount)
                {
                    publishProgress(phase1Units +
                                    dirtyUnitsCompleted * phase2Units /
                                        std::max<std::int64_t>(1, newFrameCount * channelCount));
                }
            }

            const auto removeEnd = frameIndex + numFrames;
            std::int64_t provenanceRangesCompleted = 0;
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
                    }
                    else if (range.startFrame >= removeEnd)
                    {
                        updated.push_back(
                            {.startFrame = range.startFrame - numFrames,
                             .endFrameExclusive =
                                 range.endFrameExclusive - numFrames,
                             .sourceId = range.sourceId,
                             .sourceStartFrame = range.sourceStartFrame});
                    }
                    else
                    {
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
                    ++provenanceRangesCompleted;
                    publishProgress(
                        phase1Units + phase2Units +
                        provenanceRangesCompleted * phase3Units /
                            std::max<std::int64_t>(1, totalRangeCount));
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
