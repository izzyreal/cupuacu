#pragma once
#include "AudioBlockStore.hpp"
#include "ImportAudioReader.hpp"
#include "../waveform/SourcePeaks.hpp"
#include <functional>
#include "../audio/SampleProvenance.hpp"

namespace cupuacu::storage
{
    class AudioRevision final : public AudioReader
    {
        friend class AudioRevisionBuilder;
        friend class RevisionArchive;
        static inline std::atomic<uint64_t> nextIdentity{1};
        const uint64_t identity = nextIdentity.fetch_add(1);
        AudioShape dimensions;
        std::shared_ptr<const waveform::SourcePeaks> peaks;
        std::shared_ptr<AudioBlockStore> store;
        std::shared_ptr<DecodedBlockCache> cache;
        std::vector<std::vector<AudioBlock>> channels;
        std::filesystem::path ownedSource;
        uint64_t preservationSourceId = 0;
        struct MetadataRun
        {
            int64_t start, frames;
            audio::SampleProvenance provenance;
            uint8_t dirty;
        };
        std::vector<std::vector<MetadataRun>> metadata;

        AudioRevision(AudioShape shape,
                      std::shared_ptr<AudioBlockStore> storage,
                      std::shared_ptr<DecodedBlockCache> cacheToUse)
            : dimensions(shape), store(std::move(storage)),
              cache(std::move(cacheToUse)), channels(shape.channels)
        {
        }

    public:
        AudioShape shape() const override
        {
            return dimensions;
        }
        const std::shared_ptr<const waveform::SourcePeaks> &sourcePeaks() const
        {
            return peaks;
        }
        const std::filesystem::path &sourcePath() const
        {
            return ownedSource;
        }
        const std::shared_ptr<AudioBlockStore> &blockStore() const
        {
            return store;
        }
        // Compatibility metadata for clipboard conversion. Generated audio is
        // dirty; imports retain sequential source identity; legacy clips retain
        // compact provenance runs rather than a second per-sample matrix.
        void readLegacyMetadata(int channel, int64_t start,
                                std::span<audio::SampleProvenance> provenance,
                                std::span<uint8_t> dirty) const
        {
            validateRange(dimensions, channel, start, provenance.size());
            if (dirty.size() != provenance.size())
            {
                throw std::invalid_argument("Metadata size mismatch");
            }
            if (metadata.empty() || metadata[channel].empty())
            {
                for (std::size_t i = 0; i < provenance.size(); ++i)
                {
                    provenance[i] =
                        preservationSourceId
                            ? audio::SampleProvenance{preservationSourceId,
                                                      start + int64_t(i)}
                            : audio::SampleProvenance{};
                }
                std::fill(dirty.begin(), dirty.end(),
                          ownedSource.empty() ? 1 : 0);
                return;
            }
            const auto &runs = metadata[channel];
            auto it = std::upper_bound(runs.begin(), runs.end(), start,
                                       [](int64_t frame, const MetadataRun &run)
                                       {
                                           return frame < run.start;
                                       });
            if (it == runs.begin())
            {
                throw std::logic_error("Missing sample metadata");
            }
            --it;
            while (!provenance.empty())
            {
                if (it == runs.end() || start < it->start ||
                    start >= it->start + it->frames)
                {
                    throw std::logic_error("Missing sample metadata");
                }
                const auto count = std::min<std::size_t>(
                    provenance.size(), it->start + it->frames - start);
                for (std::size_t i = 0; i < count; ++i)
                {
                    provenance[i] = it->provenance;
                    if (provenance[i].isValid())
                    {
                        provenance[i].frameIndex +=
                            start - it->start + int64_t(i);
                    }
                    dirty[i] = it->dirty;
                }
                start += count;
                provenance = provenance.subspan(count);
                dirty = dirty.subspan(count);
                ++it;
            }
        }
        void readChannel(int channel, int64_t start,
                         std::span<float> output) const override
        {
            validateRange(dimensions, channel, start, output.size());
            while (!output.empty())
            {
                const auto &block =
                    channels[channel][std::size_t(start / AudioBlockFrames)];
                const auto offset = uint32_t(start % AudioBlockFrames);
                const auto count =
                    std::min<std::size_t>(block.frames - offset, output.size());
                cache->read(*store, block, offset, output.first(count));
                output = output.subspan(count);
                start += count;
            }
        }
    };

    class AudioSlice final : public AudioReader
    {
        std::shared_ptr<const AudioReader> revision;
        int64_t start, frames;

    public:
        AudioSlice(std::shared_ptr<const AudioReader> source, int64_t first,
                   int64_t count)
            : revision(std::move(source)), start(first), frames(count)
        {
            if (!revision || start < 0 || frames < 0 ||
                start > revision->shape().frames ||
                frames > revision->shape().frames - start)
            {
                throw std::out_of_range("Audio slice outside revision");
            }
        }
        AudioShape shape() const override
        {
            auto result = revision->shape();
            result.frames = frames;
            return result;
        }
        void readChannel(int channel, int64_t first,
                         std::span<float> output) const override
        {
            validateRange(shape(), channel, first, output.size());
            revision->readChannel(channel, start + first, output);
        }
    };

    // Import transaction: only finish() exposes an immutable revision. The
    // initial index is a flat block directory; editable/paged sequence indexes
    // are a subsequent migration, not hidden behind this import builder.
    class AudioRevisionBuilder
    {
    public:
        using PendingChannel = std::array<float, AudioBlockFrames>;
        using BlockCallback = std::function<void(
            int64_t, std::span<const PendingChannel>, uint32_t)>;

    private:
        std::shared_ptr<AudioRevision> revision;
        std::vector<std::array<float, AudioBlockFrames>> pending;
        int64_t received = 0;
        uint32_t buffered = 0;
        bool finished = false;
        BlockCallback onBlock;
        std::shared_ptr<ImportAudioReader> progressive;
        void flushBlock()
        {
            try
            {
                for (std::size_t c = 0; c < pending.size(); ++c)
                {
                    revision->channels[c].push_back(revision->store->append(
                        std::span<const float>(pending[c]).first(buffered)));
                }
                if (progressive)
                {
                    std::vector<AudioBlock> blocks;
                    for (const auto &channel : revision->channels)
                    {
                        blocks.push_back(channel.back());
                    }
                    progressive->publish(blocks);
                }
                if (onBlock)
                {
                    onBlock(received - buffered, pending, buffered);
                }
                buffered = 0;
            }
            catch (...)
            {
                finished = true;
                throw;
            }
        }

    public:
        AudioRevisionBuilder(
            AudioShape shape, std::shared_ptr<AudioBlockStore> store,
            std::shared_ptr<DecodedBlockCache> cache,
            BlockCallback blockCallback = {},
            std::shared_ptr<ImportAudioReader> progressiveReader = {})
            : onBlock(std::move(blockCallback)),
              progressive(std::move(progressiveReader))
        {
            if (!store || !cache || shape.frames < 0 || shape.channels <= 0 ||
                shape.sampleRate <= 0 ||
                shape.frames >
                    INT64_MAX / shape.channels / int64_t(sizeof(float)))
            {
                throw std::invalid_argument("Invalid audio revision shape");
            }
            revision.reset(
                new AudioRevision(shape, std::move(store), std::move(cache)));
            pending.resize(shape.channels);
        }
        void appendChannelMetadata(
            int channel, int64_t start,
            std::span<const audio::SampleProvenance> provenance,
            std::span<const uint8_t> dirty)
        {
            if (finished || dirty.size() != provenance.size())
            {
                throw std::invalid_argument("Invalid metadata append");
            }
            AudioReader::validateRange(revision->dimensions, channel, start,
                                       provenance.size());
            if (revision->metadata.empty())
            {
                revision->metadata.resize(revision->dimensions.channels);
            }
            auto &runs = revision->metadata[channel];
            const auto next =
                runs.empty() ? 0 : runs.back().start + runs.back().frames;
            if (start != next)
            {
                throw std::invalid_argument("Metadata must be sequential");
            }
            for (std::size_t i = 0; i < provenance.size(); ++i)
            {
                const auto p = provenance[i];
                if (!runs.empty())
                {
                    auto &last = runs.back();
                    const bool canAdvance =
                        !last.provenance.isValid() ||
                        last.frames <= INT64_MAX - last.provenance.frameIndex;
                    const auto expectedFrame =
                        last.provenance.frameIndex +
                        (last.provenance.isValid() && canAdvance ? last.frames
                                                                 : 0);
                    if (canAdvance && last.dirty == dirty[i] &&
                        last.provenance.sourceId == p.sourceId &&
                        expectedFrame == p.frameIndex)
                    {
                        ++last.frames;
                        continue;
                    }
                }
                runs.push_back({start + int64_t(i), 1, p, dirty[i]});
            }
        }
        void appendInterleaved(std::span<const float> samples)
        {
            if (finished || samples.size() % pending.size() != 0 ||
                samples.size() / pending.size() >
                    uint64_t(revision->dimensions.frames - received))
            {
                throw std::invalid_argument("Invalid imported audio range");
            }
            std::size_t done = 0;
            const auto frames = samples.size() / pending.size();
            while (done < frames)
            {
                const auto count = std::min<std::size_t>(
                    AudioBlockFrames - buffered, frames - done);
                for (std::size_t c = 0; c < pending.size(); ++c)
                {
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        pending[c][buffered + i] =
                            samples[(done + i) * pending.size() + c];
                    }
                }
                buffered += count;
                done += count;
                received += count;
                if (buffered == AudioBlockFrames)
                {
                    flushBlock();
                }
            }
            // Publish the final partial block before consumers snapshot the
            // import's completed summaries. finish() only exposes the revision.
            if (received == revision->dimensions.frames && buffered)
            {
                flushBlock();
            }
        }
        std::shared_ptr<const AudioRevision>
        finish(std::filesystem::path ownedSource = {},
               std::shared_ptr<const waveform::SourcePeaks> peaks = {},
               uint64_t preservationSourceId = 0)
        {
            if (finished)
            {
                throw std::logic_error("Audio import already finished");
            }
            if (peaks &&
                (peaks->shape().frames != revision->dimensions.frames ||
                 peaks->shape().channels != revision->dimensions.channels))
            {
                throw std::invalid_argument(
                    "Source peaks do not match audio revision");
            }
            if (received != revision->dimensions.frames)
            {
                throw std::runtime_error(
                    "Cannot commit incomplete audio import");
            }
            if (buffered)
            {
                flushBlock();
            }
            revision->store->flush();
            for (const auto &runs : revision->metadata)
            {
                if (runs.empty() ||
                    runs.back().start + runs.back().frames != received)
                {
                    throw std::runtime_error(
                        "Cannot commit incomplete sample metadata");
                }
            }
            revision->preservationSourceId = preservationSourceId;
            revision->ownedSource = std::move(ownedSource);
            revision->peaks = std::move(peaks);
            finished = true;
            pending.clear();
            pending.shrink_to_fit();
            return std::move(revision);
        }
    };
} // namespace cupuacu::storage
