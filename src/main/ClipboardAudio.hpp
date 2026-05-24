#pragma once

#include "Document.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>

namespace cupuacu
{
    class ClipboardAudio
    {
    public:
        using SampleOperationProgressCallback =
            Document::SampleOperationProgressCallback;

        class ReadLease
        {
            const Document::AudioSegment *segment = nullptr;

        public:
            explicit ReadLease(const Document::AudioSegment *segmentToUse)
                : segment(segmentToUse)
            {
            }

            [[nodiscard]] SampleFormat getSampleFormat() const
            {
                return segment ? segment->format : SampleFormat::Unknown;
            }

            [[nodiscard]] int getSampleRate() const
            {
                return segment ? segment->sampleRate : 0;
            }

            [[nodiscard]] int64_t getFrameCount() const
            {
                return segment ? segment->frameCount : 0;
            }

            [[nodiscard]] int64_t getChannelCount() const
            {
                return segment ? segment->channelCount : 0;
            }

            [[nodiscard]] float getSample(const int64_t channel,
                                          const int64_t frame) const
            {
                return segment->samples[static_cast<std::size_t>(channel)]
                                       [static_cast<std::size_t>(frame)];
            }

            [[nodiscard]] bool isDirty(const int64_t channel,
                                       const int64_t frame) const
            {
                return segment->dirty[static_cast<std::size_t>(channel)]
                                     [static_cast<std::size_t>(frame)] != 0;
            }

            [[nodiscard]] audio::SampleProvenance
            getSampleProvenance(const int64_t channel,
                                const int64_t frame) const
            {
                return segment->provenance[static_cast<std::size_t>(channel)]
                                          [static_cast<std::size_t>(frame)];
            }
        };

    private:
        std::optional<Document::AudioSegment> segment;

        [[nodiscard]] static Document::AudioSegment
        makeInitializedSegment(const SampleFormat format,
                               const uint32_t sampleRate,
                               const uint32_t channelCount,
                               const int64_t frameCount)
        {
            Document::AudioSegment initialized{};
            initialized.format = format;
            initialized.sampleRate = static_cast<int>(sampleRate);
            initialized.channelCount = static_cast<int64_t>(channelCount);
            initialized.frameCount = std::max<int64_t>(0, frameCount);
            initialized.samples.assign(static_cast<std::size_t>(channelCount), {});
            initialized.dirty.assign(static_cast<std::size_t>(channelCount), {});
            initialized.provenance.assign(static_cast<std::size_t>(channelCount),
                                          {});

            for (uint32_t channel = 0; channel < channelCount; ++channel)
            {
                initialized.samples[channel].assign(
                    static_cast<std::size_t>(initialized.frameCount), 0.0f);
                initialized.dirty[channel].assign(
                    static_cast<std::size_t>(initialized.frameCount), 0u);
                initialized.provenance[channel].assign(
                    static_cast<std::size_t>(initialized.frameCount), {});
            }

            return initialized;
        }

    public:
        void clear()
        {
            segment.reset();
        }

        [[nodiscard]] bool hasAudio() const
        {
            return segment.has_value() && segment->channelCount > 0 &&
                   segment->frameCount > 0;
        }

        [[nodiscard]] SampleFormat getSampleFormat() const
        {
            return segment ? segment->format : SampleFormat::Unknown;
        }

        [[nodiscard]] int getSampleRate() const
        {
            return segment ? segment->sampleRate : 0;
        }

        [[nodiscard]] int64_t getFrameCount() const
        {
            return segment ? segment->frameCount : 0;
        }

        [[nodiscard]] int64_t getChannelCount() const
        {
            return segment ? segment->channelCount : 0;
        }

        [[nodiscard]] float getSample(const int64_t channel,
                                      const int64_t frame) const
        {
            return segment->samples[static_cast<std::size_t>(channel)]
                                   [static_cast<std::size_t>(frame)];
        }

        [[nodiscard]] audio::SampleProvenance
        getSampleProvenance(const int64_t channel, const int64_t frame) const
        {
            return segment->provenance[static_cast<std::size_t>(channel)]
                                      [static_cast<std::size_t>(frame)];
        }

        [[nodiscard]] ReadLease acquireReadLease() const
        {
            return ReadLease(segment ? &*segment : nullptr);
        }

        [[nodiscard]] const Document::AudioSegment *
        getWholeSegmentIfFrameCountMatches(const int64_t frameCount) const
        {
            if (!segment || frameCount != segment->frameCount)
            {
                return nullptr;
            }

            return &*segment;
        }

        void initialize(const SampleFormat format, const uint32_t sampleRate,
                        const uint32_t channelCount, const int64_t frameCount)
        {
            segment = makeInitializedSegment(format, sampleRate, channelCount,
                                             frameCount);
        }

        void setSample(const int64_t channel, const int64_t frame,
                       const float value, const bool shouldMarkDirty = true)
        {
            if (!segment)
            {
                return;
            }

            segment->samples[static_cast<std::size_t>(channel)]
                            [static_cast<std::size_t>(frame)] = value;
            segment->dirty[static_cast<std::size_t>(channel)]
                          [static_cast<std::size_t>(frame)] =
                shouldMarkDirty ? 1u : 0u;
        }

        void assignSegment(const Document::AudioSegment &segmentToCopy)
        {
            segment = segmentToCopy;
        }

        void assignSegment(Document::AudioSegment &&segmentToMove)
        {
            segment = std::move(segmentToMove);
        }

        [[nodiscard]] Document::AudioSegment captureSegment(
            const int64_t startFrame, const int64_t frameCount,
            const SampleOperationProgressCallback &progress = {}) const
        {
            Document::AudioSegment result{};
            if (!segment)
            {
                return result;
            }

            result.format = segment->format;
            result.sampleRate = segment->sampleRate;
            result.channelCount = segment->channelCount;
            result.frameCount = std::max<int64_t>(0, frameCount);
            result.samples.assign(static_cast<std::size_t>(result.channelCount),
                                  {});
            result.dirty.assign(static_cast<std::size_t>(result.channelCount), {});
            result.provenance.assign(
                static_cast<std::size_t>(result.channelCount), {});

            const auto boundedStart =
                std::clamp<int64_t>(startFrame, 0, segment->frameCount);
            const auto boundedCount = std::clamp<int64_t>(
                result.frameCount, 0, segment->frameCount - boundedStart);
            result.frameCount = boundedCount;

            constexpr int64_t kProgressStrideFrames = 16384;
            const int64_t totalProgressUnits =
                boundedCount * std::max<int64_t>(1, result.channelCount);
            for (int64_t channel = 0; channel < result.channelCount; ++channel)
            {
                auto &channelSamples =
                    result.samples[static_cast<std::size_t>(channel)];
                auto &channelDirty =
                    result.dirty[static_cast<std::size_t>(channel)];
                auto &channelProvenance =
                    result.provenance[static_cast<std::size_t>(channel)];
                channelSamples.resize(static_cast<std::size_t>(boundedCount));
                channelDirty.resize(static_cast<std::size_t>(boundedCount));
                channelProvenance.resize(static_cast<std::size_t>(boundedCount));
                for (int64_t frame = 0; frame < boundedCount; ++frame)
                {
                    const auto sourceIndex =
                        static_cast<std::size_t>(boundedStart + frame);
                    channelSamples[static_cast<std::size_t>(frame)] =
                        segment->samples[static_cast<std::size_t>(channel)]
                                        [sourceIndex];
                    channelDirty[static_cast<std::size_t>(frame)] =
                        segment->dirty[static_cast<std::size_t>(channel)]
                                      [sourceIndex];
                    channelProvenance[static_cast<std::size_t>(frame)] =
                        segment->provenance[static_cast<std::size_t>(channel)]
                                           [sourceIndex];
                    if (progress &&
                        (((frame + 1) % kProgressStrideFrames) == 0 ||
                         frame + 1 == boundedCount))
                    {
                        progress(channel * boundedCount + frame + 1,
                                 totalProgressUnits);
                    }
                }
            }

            return result;
        }

        [[nodiscard]] Document toDocument() const
        {
            Document document;
            if (segment)
            {
                document.assignSegment(*segment);
            }
            return document;
        }

        void assignDocument(const Document &document)
        {
            if (document.getChannelCount() <= 0)
            {
                clear();
                return;
            }

            assignSegment(document.captureSegment(0, document.getFrameCount()));
        }
    };
} // namespace cupuacu
