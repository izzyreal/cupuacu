#include "PitchProcessing.hpp"
#include "storage/AudioSourceBuilder.hpp"
#include "storage/WorkingAllocator.hpp"
#include "concurrency/DeferredRelease.hpp"
#include <samplerate.h>
#include <signalsmith-stretch.h>
#include <cmath>
#include <set>

namespace cupuacu::effects
{
    double pitchDisplayValue(const PitchSettings &s)
    {
        return s.unit == PitchUnit::Percent ? s.factor * 100.0
                                            : 12.0 * std::log2(s.factor);
    }

    bool setPitchDisplayValue(PitchSettings &s, const std::string &text)
    {
        try
        {
            std::size_t used = 0;
            const double value = std::stod(text, &used);
            if (used != text.size() || !std::isfinite(value))
            {
                return false;
            }
            const double factor = s.unit == PitchUnit::Percent
                                      ? value / 100.0
                                      : std::exp2(value / 12.0);
            if (!std::isfinite(factor) || factor < minPitchFactor ||
                factor > maxPitchFactor)
            {
                return false;
            }
            s.factor = factor;
            return true;
        }
        catch (const std::exception &)
        {
            return false;
        }
    }

    int64_t pitchOutputFrames(int64_t frames, const PitchSettings &s)
    {
        if (frames <= 0 || !std::isfinite(s.factor) ||
            s.factor < minPitchFactor || s.factor > maxPitchFactor ||
            (s.type != PitchType::Resample && s.type != PitchType::Stretch))
        {
            throw std::invalid_argument("Invalid pitch factor or audio range");
        }
        if (s.type == PitchType::Stretch || s.factor == 1.0)
        {
            return frames;
        }
        const long double output =
            std::round(static_cast<long double>(frames) / s.factor);
        if (output >= 9223372036854775808.0L)
        {
            throw std::overflow_error("Pitch output is too long");
        }
        return std::max<int64_t>(1, static_cast<int64_t>(output));
    }

    void remapPitchMarkers(std::span<DocumentMarker> markers, int64_t start,
                           int64_t input, int64_t output)
    {
        for (auto &marker : markers)
        {
            if (marker.frame >= start + input)
            {
                marker.frame += output - input;
            }
            else if (marker.frame > start)
            {
                marker.frame =
                    start + static_cast<int64_t>(std::round(
                                static_cast<long double>(marker.frame - start) *
                                output / input));
            }
        }
    }

    std::shared_ptr<const storage::AudioEditRevision>
    renderPitch(const storage::AudioReader &reader, int64_t start,
                int64_t frames, const std::vector<int64_t> &channels,
                const PitchSettings &settings,
                const std::filesystem::path &directory,
                const std::function<void(double)> &progress)
    {
        const auto shape = reader.shape();
        const auto outputFrames = pitchOutputFrames(frames, settings);
        if (channels.empty() || channels.size() > 2 || shape.sampleRate <= 0)
        {
            throw std::invalid_argument(
                "Invalid pitch channels or sample rate");
        }
        std::set<int64_t> unique;
        for (auto c : channels)
        {
            storage::AudioReader::validateRange(shape, int(c), start, frames);
            if (!unique.insert(c).second)
            {
                throw std::invalid_argument("Duplicate pitch channel");
            }
        }
        if (settings.type == PitchType::Resample &&
            channels.size() != std::size_t(shape.channels))
        {
            throw std::invalid_argument("Resample requires both channels");
        }
        const int count = int(channels.size());
        constexpr int block = 4096;
        auto reservation = storage::reserveWorking(
            uint64_t(count) * (4 * block * sizeof(float) + 4 * 1024 * 1024),
            storage::MemoryUse::Effect);
        double fraction = 0;
        auto check = [&]
        {
            if (progress)
            {
                progress(fraction);
            }
        };
        check();
        auto store = std::make_shared<storage::AudioBlockStore>(directory);
        storage::AudioSourceBuilder builder(
            {outputFrames, count, shape.sampleRate, shape.format}, store,
            storage::defaultDecodedBlockCache(),
            {.cancel = [&]
             {
                 check();
                 return false;
             }});
        std::vector<float> input(block * count), output(block * count),
            mono(block);
        int64_t written = 0;
        auto emit = [&](const float *data, int64_t n)
        {
            n = std::min(n, outputFrames - written);
            if (n > 0)
            {
                builder.appendInterleaved({data, std::size_t(n * count)});
            }
            written += n;
            fraction = double(written) / outputFrames;
            check();
        };
        auto read = [&](int64_t offset, int n)
        {
            std::fill(input.begin(), input.end(), 0.f);
            const auto available =
                std::max<int64_t>(0, std::min<int64_t>(n, frames - offset));
            for (int c = 0; c < count && available; ++c)
            {
                reader.readChannel(int(channels[c]), start + offset,
                                   std::span(mono).first(available));
                for (int i = 0; i < available; ++i)
                {
                    input[i * count + c] = mono[i];
                }
            }
        };
        if (settings.factor == 1.0)
        {
            for (int64_t pos = 0; pos < frames;)
            {
                const int n = int(std::min<int64_t>(block, frames - pos));
                read(pos, n);
                emit(input.data(), n);
                pos += n;
            }
        }
        else if (settings.type == PitchType::Resample)
        {
            int error = 0;
            std::unique_ptr<SRC_STATE, decltype(&src_delete)> converter(
                src_new(SRC_SINC_BEST_QUALITY, count, &error), src_delete);
            if (!converter)
            {
                throw std::runtime_error(src_strerror(error));
            }
            const double ratio = 1.0 / settings.factor;
            if ((error = src_set_ratio(converter.get(), ratio)))
            {
                throw std::runtime_error(src_strerror(error));
            }
            int64_t pos = 0;
            int available = 0, used = 0;
            while (written < outputFrames)
            {
                check();
                if (used == available && pos < frames)
                {
                    available = int(std::min<int64_t>(block, frames - pos));
                    read(pos, available);
                    pos += available;
                    used = 0;
                }
                SRC_DATA data{};
                data.data_in = input.data() + used * count;
                data.input_frames = available - used;
                data.data_out = output.data();
                data.output_frames = block;
                data.src_ratio = ratio;
                data.end_of_input = pos == frames;
                if ((error = src_process(converter.get(), &data)))
                {
                    throw std::runtime_error(src_strerror(error));
                }
                used += int(data.input_frames_used);
                emit(output.data(), data.output_frames_gen);
                if (!data.input_frames_used && !data.output_frames_gen)
                {
                    if (pos != frames || used != available)
                    {
                        throw std::runtime_error(
                            "Pitch resampler made no progress");
                    }
                    // libsamplerate rounds down a fractional final frame.
                    if (outputFrames - written > 1)
                    {
                        throw std::runtime_error("Pitch resampler ended early");
                    }
                    std::fill(output.begin(), output.end(), 0.f);
                    emit(output.data(), outputFrames - written);
                }
            }
        }
        else
        {
            signalsmith::stretch::SignalsmithStretch<float> stretch;
            stretch.presetDefault(count, shape.sampleRate);
            stretch.setTransposeFactor(settings.factor);
            const int inputLatency = stretch.inputLatency();
            const int outputLatency = stretch.outputLatency();
            if (frames > INT64_MAX - inputLatency)
            {
                throw std::overflow_error("Pitch input is too long");
            }
            int64_t discard = int64_t(inputLatency) + outputLatency;
            const int capacity = std::max(block, outputLatency);
            std::vector<std::vector<float>> ins(count,
                                                std::vector<float>(block));
            std::vector<std::vector<float>> outs(count,
                                                 std::vector<float>(capacity));
            output.resize(std::size_t(capacity) * count);
            auto collect = [&](int n)
            {
                const int skip = int(std::min<int64_t>(discard, n));
                discard -= skip;
                for (int i = skip; i < n; ++i)
                {
                    for (int c = 0; c < count; ++c)
                    {
                        output[(i - skip) * count + c] = outs[c][i];
                    }
                }
                emit(output.data(), n - skip);
            };
            // Feed the input latency as silence, then flush the output latency.
            for (int64_t pos = 0; pos < frames + inputLatency;)
            {
                check();
                const int n =
                    int(std::min<int64_t>(block, frames + inputLatency - pos));
                read(pos, n);
                for (int c = 0; c < count; ++c)
                {
                    for (int i = 0; i < n; ++i)
                    {
                        ins[c][i] = input[i * count + c];
                    }
                }
                stretch.process(ins, n, outs, n);
                collect(n);
                pos += n;
            }
            stretch.flush(outs, outputLatency);
            collect(outputLatency);
        }
        if (written != outputFrames)
        {
            throw std::runtime_error("Incomplete pitch output");
        }
        check();
        return concurrency::releaseOnWorker(
            storage::AudioEditRevision::from(builder.finish()));
    }
} // namespace cupuacu::effects
