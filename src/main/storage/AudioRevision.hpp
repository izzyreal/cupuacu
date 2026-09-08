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
        std::vector<RecordIndex<AudioBlock>> channels;
        std::filesystem::path ownedSource;
        std::shared_ptr<const void> cacheLease;
        uint64_t preservationSourceId = 0;
        uint64_t metadataSourceId = 0;
        struct MetadataRun
        {
            int64_t start, frames;
            audio::SampleProvenance provenance;
            uint8_t dirty;
        };
        std::vector<RecordIndex<MetadataRun>> metadata;

        AudioRevision(AudioShape shape,
                      std::shared_ptr<AudioBlockStore> storage,
                      std::shared_ptr<DecodedBlockCache> cacheToUse)
            : dimensions(shape), store(std::move(storage)),
              cache(std::move(cacheToUse)), channels(shape.channels)
        {
        }

    public:
        std::shared_ptr<const AudioRevision> withSampleCache(
            std::shared_ptr<DecodedBlockCache> samples,
            std::shared_ptr<const void> lease = {},
            uint64_t importedSourceId = 0) const
        {
            auto value = std::shared_ptr<AudioRevision>(
                new AudioRevision(dimensions, store, std::move(samples)));
            value->peaks = peaks;
            value->channels = channels;
            value->ownedSource = ownedSource;
            value->preservationSourceId = preservationSourceId;
            value->metadataSourceId = metadataSourceId;
            value->metadata = metadata;
            if (importedSourceId)
            {
                value->preservationSourceId = importedSourceId;
            }
            value->cacheLease = std::move(lease);
            return value;
        }
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
            uint64_t lo = 0, hi = runs.size();
            while (lo < hi)
            {
                const auto mid = lo + (hi - lo) / 2;
                if (runs[mid].start <= start)
                {
                    lo = mid + 1;
                }
                else
                {
                    hi = mid;
                }
            }
            if (!lo)
            {
                throw std::logic_error("Missing sample metadata");
            }
            auto index = lo - 1;
            while (!provenance.empty())
            {
                const auto run = runs[index];
                if (start < run.start || start >= run.start + run.frames)
                {
                    throw std::logic_error("Missing sample metadata");
                }
                const auto count = std::min<std::size_t>(
                    provenance.size(), run.start + run.frames - start);
                for (std::size_t i = 0; i < count; ++i)
                {
                    provenance[i] = run.provenance;
                    if (provenance[i].sourceId == metadataSourceId)
                    {
                        provenance[i].sourceId = preservationSourceId;
                    }
                    if (provenance[i].isValid())
                    {
                        provenance[i].frameIndex +=
                            start - run.start + int64_t(i);
                    }
                    dirty[i] = run.dirty;
                }
                start += count;
                provenance = provenance.subspan(count);
                dirty = dirty.subspan(count);
                ++index;
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
        void readDirtyFlags(int channel, int64_t start,
                            std::span<uint8_t> output) const override
        {
            validateRange(dimensions, channel, start, output.size());
            if (metadata.empty())
            {
                std::fill(output.begin(), output.end(),
                          ownedSource.empty() ? 1 : 0);
                return;
            }
            std::array<audio::SampleProvenance, 1024> scratch;
            while (!output.empty())
            {
                const auto count = std::min(output.size(), scratch.size());
                readLegacyMetadata(channel, start,
                                   std::span(scratch).first(count),
                                   output.first(count));
                start += count;
                output = output.subspan(count);
            }
        }
        struct IndexStats
        {
            uint64_t records = 0, residentBytes = 0, diskBytes = 0;
        };
        IndexStats indexStats() const
        {
            IndexStats result;
            const auto add = [&](const auto &indexes)
            {
                for (const auto &index : indexes)
                {
                    const auto s = index.stats();
                    result.records += s.records;
                    result.residentBytes += s.residentBytes;
                    result.diskBytes += s.diskBytes;
                }
            };
            add(channels);
            add(metadata);
            return result;
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
    // directories are bounded working indexes shared with progressive readers.
    class AudioRevisionBuilder
    {
    public:
        using PendingChannel = std::array<float, AudioBlockFrames>;
        using BlockCallback = std::function<void(
            int64_t, std::span<const PendingChannel>, uint32_t)>;

    private:
        std::shared_ptr<AudioRevision> revision;
        std::shared_ptr<void> pendingMemory;
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
                std::vector<AudioBlock> blocks;
                blocks.reserve(pending.size());
                for (std::size_t c = 0; c < pending.size(); ++c)
                {
                    blocks.push_back(revision->store->append(
                        std::span<const float>(pending[c]).first(buffered)));
                    if (!progressive)
                    {
                        revision->channels[c].push_back(blocks.back());
                    }
                }
                if (progressive)
                {
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
            if (progressive &&
                (progressive->shape().frames != shape.frames ||
                 progressive->shape().channels != shape.channels ||
                 progressive->shape().sampleRate != shape.sampleRate ||
                 progressive->shape().format != shape.format ||
                 progressive->availableFrames()))
            {
                throw std::invalid_argument(
                    "Mismatched progressive audio index");
            }
            revision.reset(
                new AudioRevision(shape, std::move(store), std::move(cache)));
            pendingMemory = reserveWorking(uint64_t(shape.channels) *
                                               sizeof(PendingChannel),
                                           MemoryUse::Import);
            pending.resize(shape.channels);
            if (progressive)
            {
                revision->channels = progressive->blockIndexes();
            }
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
            if (provenance.empty())
            {
                return;
            }
            auto last =
                runs.empty() ? AudioRevision::MetadataRun{} : runs.back();
            bool existing = !runs.empty();
            for (std::size_t i = 0; i < provenance.size(); ++i)
            {
                const auto p = provenance[i];
                if (last.frames)
                {
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
                if (last.frames)
                {
                    if (existing)
                    {
                        runs.setBack(last);
                    }
                    else
                    {
                        runs.push_back(last);
                    }
                }
                last = {start + int64_t(i), 1, p, dirty[i]};
                existing = false;
            }
            if (existing)
            {
                runs.setBack(last);
            }
            else
            {
                runs.push_back(last);
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
            revision->metadataSourceId = preservationSourceId;
            for (auto &channel : revision->channels)
            {
                channel.seal();
            }
            for (auto &runs : revision->metadata)
            {
                runs.seal();
            }
            revision->ownedSource = std::move(ownedSource);
            revision->peaks = std::move(peaks);
            finished = true;
            std::vector<PendingChannel>().swap(pending);
            pendingMemory.reset();
            return std::move(revision);
        }
    };
} // namespace cupuacu::storage
