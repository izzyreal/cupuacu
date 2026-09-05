#pragma once

#include "AudioWindow.hpp"
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace cupuacu::storage
{
    // One immutable revision per service. Requests contain no owning storage
    // references: superseding a request cannot reclaim files on the caller.
    // This is a viewport service, not a realtime transport API or scheduler.
    class AsyncAudioReader
    {
    public:
        struct Result
        {
            uint64_t generation = 0;
            int channel = 0;
            int64_t start = 0;
            std::vector<float> samples;
            std::exception_ptr error;
        };

        AsyncAudioReader(std::shared_ptr<const AudioReader> reader,
                         std::size_t maxWindowFrames)
            : state(std::make_shared<State>())
        {
            if (!reader || maxWindowFrames == 0 ||
                maxWindowFrames > std::vector<float>().max_size())
            {
                throw std::invalid_argument(
                    "Invalid asynchronous audio reader limit");
            }
            state->shape = reader->shape();
            state->maxFrames = maxWindowFrames;
            // The thread owns the reader and releases it before signaling
            // completion. It never refers to this service or a GUI object.
            std::thread(
                [shared = state, reader = std::move(reader)]() mutable
                {
                    run(*shared, *reader);
                    reader.reset();
                    {
                        std::lock_guard lock(shared->mutex);
                        shared->finished = true;
                    }
                    shared->cv.notify_all();
                })
                .detach();
        }

        ~AsyncAudioReader()
        {
            close();
        }
        AsyncAudioReader(const AsyncAudioReader &) = delete;
        AsyncAudioReader &operator=(const AsyncAudioReader &) = delete;

        uint64_t submit(int channel, int64_t start, std::size_t frames)
        {
            AudioReader::validateRange(state->shape, channel, start, frames);
            if (frames > state->maxFrames)
            {
                throw std::length_error("Audio viewport exceeds window budget");
            }
            std::lock_guard lock(state->mutex);
            if (state->closed)
            {
                throw std::logic_error("Audio reader is closed");
            }
            const auto generation = ++state->generation;
            state->pending = Request{generation, channel, start, frames};
            state->published.reset();
            state->cv.notify_one();
            return generation;
        }

        std::optional<Result> takePublished()
        {
            std::lock_guard lock(state->mutex);
            return std::exchange(state->published, std::nullopt);
        }

        // Does not wait for an in-flight read or working-file reclamation.
        void close()
        {
            std::lock_guard lock(state->mutex);
            state->closed = true;
            state->pending.reset();
            state->published.reset();
            state->cv.notify_all();
        }

        // For shutdown coordination/tests on a NON-UI thread, after close().
        void waitUntilClosed()
        {
            std::unique_lock lock(state->mutex);
            if (!state->closed)
            {
                throw std::logic_error("Close audio reader before waiting");
            }
            state->cv.wait(lock,
                           [&]
                           {
                               return state->finished;
                           });
        }

    private:
        struct Request
        {
            uint64_t generation;
            int channel;
            int64_t start;
            std::size_t frames;
        };
        struct State
        {
            AudioShape shape;
            std::size_t maxFrames = 0;
            std::mutex mutex;
            std::condition_variable cv;
            bool closed = false, finished = false;
            uint64_t generation = 0;
            std::optional<Request> pending;
            std::optional<Result> published;
        };

        static void run(State &state, const AudioReader &reader)
        {
            for (;;)
            {
                Request request;
                {
                    std::unique_lock lock(state.mutex);
                    state.cv.wait(lock,
                                  [&]
                                  {
                                      return state.closed ||
                                             state.pending.has_value();
                                  });
                    if (state.closed)
                    {
                        return;
                    }
                    request = *state.pending;
                    state.pending.reset();
                }
                const auto canceled = [&]
                {
                    std::lock_guard lock(state.mutex);
                    return state.closed ||
                           request.generation != state.generation;
                };
                Result result{
                    request.generation, request.channel, request.start, {}, {}};
                try
                {
                    if (canceled())
                    {
                        continue;
                    }
                    result.samples.resize(request.frames);
                    if (!readAudioWindow(reader, request.channel, request.start,
                                         result.samples, canceled))
                    {
                        continue;
                    }
                }
                catch (...)
                {
                    result.samples.clear();
                    result.error = std::current_exception();
                }
                std::lock_guard lock(state.mutex);
                if (!state.closed && request.generation == state.generation)
                {
                    state.published = std::move(result);
                }
            }
        }

        std::shared_ptr<State> state;
    };
} // namespace cupuacu::storage
