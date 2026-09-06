#pragma once
#include "AudioWindow.hpp"
#include "WorkingMemory.hpp"
#include "../concurrency/LatestValueWorker.hpp"

namespace cupuacu::storage
{
    class AsyncAudioReader
    {
    public:
        struct Result
        {
            std::shared_ptr<void> memory;
            uint64_t generation = 0;
            int channel = 0;
            int64_t start = 0;
            std::vector<float> samples;
            std::exception_ptr error;
            bool firstSampleDirty = true;
            Result() = default;
            Result(const Result &) = delete;
            Result &operator=(const Result &) = delete;
            Result(Result &&) noexcept = default;
            Result &operator=(Result &&other) noexcept
            {
                if (this != &other)
                {
                    Result old(std::move(*this));
                    memory = std::move(other.memory);
                    generation = other.generation;
                    channel = other.channel;
                    start = other.start;
                    samples = std::move(other.samples);
                    error = std::move(other.error);
                    firstSampleDirty = other.firstSampleDirty;
                }
                return *this;
            }
        };

    private:
        struct Request
        {
            int channel;
            int64_t start;
            std::size_t frames;
        };
        using Worker = concurrency::LatestValueWorker<Request, Result>;
        AudioShape dimensions;
        std::size_t maxFrames;
        Worker worker;
        static AudioShape
        checkedShape(const std::shared_ptr<const AudioReader> &reader,
                     std::size_t limit)
        {
            if (!reader || !limit || limit > std::vector<float>().max_size())
            {
                throw std::invalid_argument(
                    "Invalid asynchronous audio reader limit");
            }
            return reader->shape();
        }

    public:
        AsyncAudioReader(std::shared_ptr<const AudioReader> reader,
                         std::size_t maxWindowFrames)
            : dimensions(checkedShape(reader, maxWindowFrames)),
              maxFrames(maxWindowFrames),
              worker(
                  [reader = std::move(reader)](
                      const Request &request, const Worker::CancelCheck &cancel)
                      -> std::optional<Result>
                  {
                      Result result;
                      result.channel = request.channel;
                      result.start = request.start;
                      try
                      {
                          result.memory =
                              reserveWorking(request.frames * sizeof(float),
                                             MemoryUse::Viewport);
                          result.samples.resize(request.frames);
                          if (!readAudioWindow(*reader, request.channel,
                                               request.start, result.samples,
                                               cancel))
                          {
                              return {};
                          }
                          if (request.frames == 1)
                          {
                              uint8_t dirty = 1;
                              reader->readDirtyFlags(
                                  request.channel, request.start, {&dirty, 1});
                              result.firstSampleDirty = dirty != 0;
                          }
                      }
                      catch (...)
                      {
                          result.samples.clear();
                          result.error = std::current_exception();
                      }
                      return result;
                  })
        {
        }
        uint64_t submit(int channel, int64_t start, std::size_t frames)
        {
            AudioReader::validateRange(dimensions, channel, start, frames);
            if (frames > maxFrames)
            {
                throw std::length_error("Audio viewport exceeds window budget");
            }
            return worker.submit({channel, start, frames});
        }
        std::optional<Result> takePublished()
        {
            auto published = worker.takePublished();
            if (!published)
            {
                return {};
            }
            Result result =
                published->value ? std::move(*published->value) : Result{};
            result.generation = published->generation;
            if (published->error)
            {
                result.error = published->error;
            }
            return result;
        }
        void close()
        {
            worker.close();
        }
        // Non-UI shutdown coordination only.
        void waitUntilClosed()
        {
            worker.waitUntilClosed();
        }
    };
} // namespace cupuacu::storage
