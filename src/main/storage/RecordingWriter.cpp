#include "RecordingWriter.hpp"
#include "../concurrency/DeferredRelease.hpp"
#include <chrono>

namespace cupuacu::storage
{
    RecordingWriter::RecordingWriter(
        std::shared_ptr<const AudioEditRevision> before, int64_t first,
        std::filesystem::path path)
        : original(std::move(before)), start(first), directory(std::move(path)),
          memory(reserveWorking(sizeof(Queue) +
                                    batchFrames * audio::kMaxRecordedChannels *
                                        sizeof(float),
                                MemoryUse::Transport)),
          queue(std::make_unique<Queue>())
    {
        if (!original || start < 0 || start > original->shape().frames ||
            original->shape().channels > int(audio::kMaxRecordedChannels))
        {
            throw std::invalid_argument("Invalid recording destination");
        }
        publication.audio = original;
        publication.endFrame = start;
    }
    void RecordingWriter::startWorker()
    {
        if (worker.joinable())
        {
            throw std::logic_error("Recording worker already started");
        }
        worker = std::thread(
            [this]
            {
                run();
            });
    }
    RecordingWriter::~RecordingWriter()
    {
        cancel();
        if (worker.joinable())
        {
            worker.join();
        }
    }
    bool RecordingWriter::submit(const audio::RecordedChunk &chunk) noexcept
    {
        if (finishing.load(std::memory_order_acquire) ||
            canceled.load(std::memory_order_acquire))
        {
            return false;
        }
        const auto next = head.load(std::memory_order_relaxed);
        const auto consumed = tail.load(std::memory_order_acquire);
        if (next - consumed == queueChunks)
        {
            overflow.store(true, std::memory_order_relaxed);
            finish();
            return false;
        }
        (*queue)[next % queueChunks] = chunk;
        head.store(next + 1, std::memory_order_release);
        highWater.store(std::max(highWater.load(), next + 1 - consumed));
        return true;
    }
    void RecordingWriter::finish() noexcept
    {
        finishing.store(true, std::memory_order_release);
    }
    void RecordingWriter::cancel() noexcept
    {
        canceled.store(true, std::memory_order_release);
    }
    RecordingWriter::Snapshot RecordingWriter::snapshot() const
    {
        std::lock_guard lock(publicationMutex);
        return publication;
    }
    void RecordingWriter::run() noexcept
    {
        try
        {
            auto store = std::make_shared<AudioBlockStore>(directory);
            auto cache = defaultDecodedBlockCache();
            auto current = original;
            auto shape = original->shape();
            std::array<float, batchFrames * audio::kMaxRecordedChannels>
                samples;
            int64_t buffered = 0, received = start, written = start;
            auto flush = [&]
            {
                if (!buffered)
                {
                    return;
                }
                auto batchShape = shape;
                batchShape.frames = buffered;
                AudioRevisionBuilder builder(batchShape, store, cache);
                builder.appendInterleaved(
                    std::span(samples).first(buffered * shape.channels));
                // Build summaries from the captured scratch, never reread
                // audio.
                auto peaks = waveform::SourcePeaks::createStreaming(
                    batchShape,
                    [&](int c, uint64_t first, std::span<gui::Peak> out)
                    {
                        for (std::size_t p = 0; p < out.size(); ++p)
                        {
                            auto peak = waveform::emptyPeak();
                            const auto begin =
                                int64_t(first + p) *
                                waveform::SourcePeaks::blockFrames;
                            for (auto f = begin;
                                 f <
                                 std::min(
                                     buffered,
                                     begin +
                                         waveform::SourcePeaks::blockFrames);
                                 ++f)
                            {
                                const auto value =
                                    samples[f * shape.channels + c];
                                peak = waveform::combine(peak, {value, value});
                            }
                            out[p] = peak;
                        }
                    },
                    cache);
                auto inserted = AudioEditRevision::from(
                    builder.finish({}, std::move(peaks)));
                AudioEditTransaction edit(*current);
                const auto overwritten =
                    std::min(buffered, current->shape().frames - written);
                edit.replace(written, overwritten, *inserted);
                current = edit.finish();
                written += buffered;
                buffered = 0;
                Snapshot next{concurrency::releaseOnWorker(current),
                              written,
                              false,
                              {},
                              store->ioBytes().second};
                // Release the superseded publication on this worker.
                {
                    std::lock_guard lock(publicationMutex);
                    std::swap(publication, next);
                }
            };
            for (;;)
            {
                if (canceled.load(std::memory_order_acquire))
                {
                    break;
                }
                const auto next = tail.load(std::memory_order_relaxed);
                if (next == head.load(std::memory_order_acquire))
                {
                    if (finishing.load(std::memory_order_acquire))
                    {
                        // Recheck after acquiring finish: producer's last chunk
                        // precedes that release even if the first head read
                        // lagged.
                        if (next != head.load(std::memory_order_acquire))
                        {
                            continue;
                        }
                        flush();
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                const auto chunk = (*queue)[next % queueChunks];
                tail.store(next + 1, std::memory_order_release);
                if (!chunk.frameCount ||
                    chunk.frameCount > audio::kRecordedChunkFrames ||
                    chunk.channelCount != shape.channels ||
                    chunk.startFrame != received ||
                    received > INT64_MAX - chunk.frameCount)
                {
                    throw std::runtime_error(
                        "Invalid or discontinuous recording chunk");
                }
                for (uint32_t f = 0; f < chunk.frameCount; ++f)
                {
                    for (int c = 0; c < shape.channels; ++c)
                    {
                        samples[buffered * shape.channels + c] =
                            chunk.interleavedSamples
                                [f * audio::kMaxRecordedChannels + c];
                    }
                    ++buffered;
                    ++received;
                    if (buffered == batchFrames)
                    {
                        flush();
                    }
                }
            }
            if (overflow.load())
            {
                std::lock_guard lock(publicationMutex);
                publication.error =
                    "Recording storage could not keep up. Recording stopped "
                    "before missing audio.";
            }
        }
        catch (const std::exception &e)
        {
            std::lock_guard lock(publicationMutex);
            publication.error = e.what();
        }
        finishing.store(true, std::memory_order_release);
        std::lock_guard lock(publicationMutex);
        publication.completed = true;
    }
} // namespace cupuacu::storage
