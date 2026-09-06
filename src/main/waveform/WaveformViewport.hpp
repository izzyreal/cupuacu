#pragma once
#include "../concurrency/LatestValueWorker.hpp"
#include "../storage/AudioWindow.hpp"
#include "SourcePeaks.hpp"
#include <cmath>

namespace cupuacu::waveform
{
    struct ViewportSource
    {
        std::shared_ptr<const storage::AudioReader> audio;
        std::function<std::optional<Peak>(int, int64_t, int64_t)> overview;
        std::function<bool(const std::function<bool()> &)> prepare;
        std::function<int64_t()> availableFrames;
        std::function<int64_t()> overviewAvailableFrames;
    };
    struct ViewportRequest
    {
        int channel = 0;
        int64_t offset = 0;
        double samplesPerPixel = 1;
        int width = 0;
        bool operator==(const ViewportRequest &) const = default;
    };
    struct ViewportData
    {
        ViewportRequest request;
        int64_t rawStart = 0;
        std::vector<float> samples;
        std::vector<Peak> peaks;
        bool pending = false;
        int64_t availableFrames = 0;
        float sampleAt(int64_t frame) const
        {
            const auto local = frame - rawStart;
            return local >= 0 && uint64_t(local) < samples.size()
                       ? samples[local]
                       : 0.0f;
        }
    };
    class WaveformViewport
    {
        using Worker =
            concurrency::LatestValueWorker<ViewportRequest, ViewportData>;
        storage::AudioShape shape;
        Worker worker;
        static storage::AudioShape checkedShape(const ViewportSource &source)
        {
            if (!source.audio)
            {
                throw std::invalid_argument("Missing viewport reader");
            }
            return source.audio->shape();
        }

    public:
        static constexpr int maxWidth = 16384;
        static constexpr std::size_t maxSampleFrames = (maxWidth + 1) * 128 + 8;
        using Result = Worker::Result;
        explicit WaveformViewport(ViewportSource source)
            : shape(checkedShape(source)),
              worker(
                  [source =
                       std::move(source)](const ViewportRequest &request,
                                          const Worker::CancelCheck &cancel)
                  {
                      return compute(source, request, cancel);
                  })
        {
        }
        static void validate(storage::AudioShape shape,
                             const ViewportRequest &request)
        {
            if (request.channel < 0 || request.channel >= shape.channels ||
                request.offset < 0 || request.offset > shape.frames ||
                request.width <= 0 || request.width > maxWidth ||
                !std::isfinite(request.samplesPerPixel) ||
                request.samplesPerPixel <= 0)
            {
                throw std::invalid_argument("Invalid waveform viewport");
            }
        }
        uint64_t submit(ViewportRequest request)
        {
            validate(shape, request);
            return worker.submit(request);
        }
        std::optional<Result> takePublished()
        {
            return worker.takePublished();
        }
        void close()
        {
            worker.close();
        }
        void waitUntilClosed()
        {
            worker.waitUntilClosed();
        }
        // Worker-only pure planning entry point, also used by benchmarks.
        static std::optional<ViewportData>
        compute(const ViewportSource &source, ViewportRequest request,
                const Worker::CancelCheck &cancel)
        {
            const auto shape = source.audio->shape();
            validate(shape, request);
            ViewportData result{request, 0, {}, {}, false};
            const auto &availability =
                request.samplesPerPixel >= 128 && source.overviewAvailableFrames
                    ? source.overviewAvailableFrames
                    : source.availableFrames;
            result.availableFrames =
                availability ? availability() : shape.frames;
            if (cancel())
            {
                return {};
            }
            const auto frameAt = [&](double x)
            {
                const long double value =
                    static_cast<long double>(request.offset) +
                    static_cast<long double>(x) * request.samplesPerPixel;
                const long double rounded =
                    std::floor(static_cast<double>(value));
                return rounded >= shape.frames ? shape.frames
                       : rounded <= 0          ? int64_t{0}
                                               : int64_t(rounded);
            };
            if (request.samplesPerPixel >= 128)
            {
                if (!source.overview ||
                    (source.prepare && !source.prepare(cancel)))
                {
                    result.pending = true;
                    return cancel() ? std::nullopt
                                    : std::optional{std::move(result)};
                }
                result.peaks.resize(request.width, emptyPeak());
                for (int x = 0; x < request.width; ++x)
                {
                    if ((x % 64 == 0) && cancel())
                    {
                        return {};
                    }
                    const auto first = frameAt(x),
                               end = std::min(frameAt(x + 1),
                                              result.availableFrames);
                    if (end > first)
                    {
                        auto peak = source.overview(request.channel, first,
                                                    end - first);
                        if (!peak)
                        {
                            result.pending = true;
                            return result;
                        }
                        result.peaks[x] = *peak;
                    }
                }
            }
            else
            {
                result.rawStart = std::max<int64_t>(0, request.offset - 3);
                const auto end = std::min(
                    shape.frames,
                    frameAt(request.width + 1) +
                        std::min<int64_t>(4, shape.frames -
                                                 frameAt(request.width + 1)));
                if (source.availableFrames && end > source.availableFrames())
                {
                    result.pending = true;
                    return result;
                }
                const auto count = uint64_t(end - result.rawStart);
                if (count > maxSampleFrames)
                {
                    throw std::length_error("Viewport sample budget exceeded");
                }
                result.samples.resize(count);
                if (!storage::readAudioWindow(*source.audio, request.channel,
                                              result.rawStart, result.samples,
                                              cancel))
                {
                    return {};
                }
                if (request.samplesPerPixel >= 1)
                {
                    result.peaks.resize(request.width, emptyPeak());
                    for (int x = 0; x < request.width; ++x)
                    {
                        if ((x % 64 == 0) && cancel())
                        {
                            return {};
                        }
                        for (auto frame = frameAt(x); frame < frameAt(x + 1);
                             ++frame)
                        {
                            const auto value = result.sampleAt(frame);
                            result.peaks[x] =
                                combine(result.peaks[x], {value, value});
                        }
                    }
                    result.samples.clear();
                    result.samples.shrink_to_fit();
                }
            }
            return cancel() ? std::nullopt : std::optional{std::move(result)};
        }
    };
} // namespace cupuacu::waveform
