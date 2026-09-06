#include "StreamingPeakBuilder.hpp"
#include "ProgressivePeaks.hpp"
#include "../storage/AudioBlockStore.hpp"
#include "../LongTask.hpp"
#include <chrono>

namespace cupuacu::waveform
{
    struct StreamingPeakBuilder::Impl
    {
        storage::AudioShape shape;
        std::shared_ptr<storage::DecodedBlockCache> cache;
        std::function<bool()> cancel;
        std::shared_ptr<ProgressivePeaks> progressive;
        int64_t built = 0;
        bool finished = false, failed = false;
        std::shared_ptr<void> memory;
        std::shared_ptr<storage::AudioBlockStore> spool;
        std::vector<std::ofstream> writers;
        std::vector<std::vector<Peak>> small;
        std::vector<Peak> partial;
        void check() const
        {
            if (cancel && cancel())
            {
                throw LongTaskCanceledError();
            }
        }
        std::filesystem::path path(int c) const
        {
            return spool->path() / (std::to_string(c) + ".peaks");
        }
        Impl(storage::AudioShape shape,
             std::shared_ptr<storage::DecodedBlockCache> cache,
             std::function<bool()> cancel,
             std::shared_ptr<ProgressivePeaks> progressive)
            : shape(shape), cache(std::move(cache)), cancel(std::move(cancel)),
              progressive(std::move(progressive))
        {
            if (shape.frames < 0 || shape.channels <= 0)
            {
                throw std::invalid_argument("Invalid peak shape");
            }
            check();
            const auto baseCount =
                shape.frames / 128 + (shape.frames % 128 != 0);
            const uint64_t retained =
                !this->progressive &&
                        baseCount <= int64_t(SourcePeaks::residentLevelLimit)
                    ? uint64_t(baseCount) * shape.channels * sizeof(Peak)
                    : 0;
            memory = storage::reserveWorking(
                retained + uint64_t(shape.channels) * sizeof(Peak) +
                    65536 * sizeof(float) + 513 * sizeof(Peak),
                storage::MemoryUse::Peaks);
            partial.resize(shape.channels, emptyPeak());
            if (this->progressive)
            {
                return;
            }
            const auto count = shape.frames / 128 + (shape.frames % 128 != 0);
            if (count <= int64_t(SourcePeaks::residentLevelLimit))
            {
                small.resize(shape.channels);
                for (auto &channel : small)
                {
                    channel.reserve(count);
                }
            }
            else
            {
                static std::atomic<uint64_t> sequence{0};
                spool = std::make_shared<storage::AudioBlockStore>(
                    std::filesystem::temp_directory_path() /
                    ("cupuacu-peak-spool-" +
                     std::to_string(std::chrono::steady_clock::now()
                                        .time_since_epoch()
                                        .count()) +
                     "-" + std::to_string(sequence.fetch_add(1))));
                writers.resize(shape.channels);
                for (int c = 0; c < shape.channels; ++c)
                {
                    writers[c].open(path(c),
                                    std::ios::binary | std::ios::trunc);
                    if (!writers[c])
                    {
                        throw std::runtime_error("Cannot create peak spool");
                    }
                }
            }
        }
    };
    StreamingPeakBuilder::StreamingPeakBuilder(
        storage::AudioShape shape,
        std::shared_ptr<storage::DecodedBlockCache> cache,
        std::function<bool()> cancel,
        std::shared_ptr<ProgressivePeaks> progressive)
        : impl(std::make_unique<Impl>(shape, std::move(cache),
                                      std::move(cancel),
                                      std::move(progressive)))
    {
    }
    StreamingPeakBuilder::~StreamingPeakBuilder() = default;
    void StreamingPeakBuilder::appendFrom(storage::AudioShape shape,
                                          int64_t available,
                                          const ReadAudio &read)
    {
        auto &s = *impl;
        if (s.finished || s.failed || shape.frames != s.shape.frames ||
            shape.channels != s.shape.channels ||
            shape.sampleRate != s.shape.sampleRate ||
            shape.format != s.shape.format || available < s.built ||
            available > shape.frames)
        {
            throw std::logic_error("Invalid peak append");
        }
        // A reader/write exception poisons this transaction; partially written
        // channels must never be exposed by finish or retried at the old
        // offset.
        s.failed = true;
        std::array<float, 65536> samples;
        std::array<Peak, 513> peaks;
        for (int c = 0; c < shape.channels; ++c)
        {
            for (int64_t first = s.built; first < available;)
            {
                s.check();
                const auto count =
                    std::min<int64_t>(samples.size(), available - first);
                read(c, first, std::span(samples).first(count));
                std::size_t emitted = 0;
                for (int64_t at = 0; at < count;)
                {
                    const auto inBucket = (first + at) % 128;
                    const auto end = std::min(count, at + 128 - inBucket);
                    auto peak = s.partial[c];
                    if (!inBucket)
                    {
                        // Match the existing waveform builder's first-sample
                        // semantics, including leading NaNs and signed zero.
                        peak = {samples[at], samples[at]};
                        ++at;
                    }
                    for (; at < end; ++at)
                    {
                        peak = combine(peak, {samples[at], samples[at]});
                    }
                    s.partial[c] = peak;
                    if ((first + at) % 128 == 0 || first + at == shape.frames)
                    {
                        peaks[emitted++] = peak;
                    }
                }
                if (s.progressive)
                {
                    s.progressive->append(c, std::span(peaks).first(emitted));
                }
                else if (s.spool)
                {
                    static_assert(sizeof(Peak) == 8);
                    s.writers[c].write(
                        reinterpret_cast<const char *>(peaks.data()),
                        emitted * sizeof(Peak));
                    if (!s.writers[c])
                    {
                        throw std::runtime_error("Peak spool write failed");
                    }
                }
                else
                {
                    s.small[c].insert(s.small[c].end(), peaks.begin(),
                                      peaks.begin() + emitted);
                }
                first += count;
            }
        }
        s.built = available;
        if (s.progressive)
        {
            s.progressive->publish(available);
        }
        s.failed = false;
    }
    std::shared_ptr<const SourcePeaks> StreamingPeakBuilder::finish()
    {
        auto &s = *impl;
        if (s.finished || s.failed || s.built != s.shape.frames)
        {
            throw std::logic_error("Incomplete peak build");
        }
        s.finished = true;
        s.check();
        if (s.progressive)
        {
            auto result = s.progressive->finish();
            std::vector<Peak>().swap(s.partial);
            s.memory.reset();
            return result;
        }
        for (auto &writer : s.writers)
        {
            writer.close();
            if (!writer)
            {
                throw std::runtime_error("Peak spool close failed");
            }
        }
        std::ifstream input;
        int previous = -1;
        auto result = SourcePeaks::createStreaming(
            s.shape,
            [&](int c, uint64_t first, std::span<Peak> output)
            {
                s.check();
                if (!s.spool)
                {
                    std::copy_n(s.small[c].begin() + first, output.size(),
                                output.begin());
                }
                else
                {
                    if (c != previous)
                    {
                        input = std::ifstream(s.path(c), std::ios::binary);
                        previous = c;
                    }
                    input.seekg(std::streamoff(first * sizeof(Peak)));
                    input.read(reinterpret_cast<char *>(output.data()),
                               output.size_bytes());
                    if (!input)
                    {
                        throw std::runtime_error("Peak spool read failed");
                    }
                }
            },
            s.cache, s.cancel);
        input.close();
        s.writers.clear();
        s.spool.reset();
        s.small.clear();
        std::vector<Peak>().swap(s.partial);
        s.memory.reset();
        return result;
    }
} // namespace cupuacu::waveform
