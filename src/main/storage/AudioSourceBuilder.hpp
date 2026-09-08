#pragma once

#include "AudioRevision.hpp"
#include "../waveform/StreamingPeakBuilder.hpp"
#include "../LongTask.hpp"

namespace cupuacu::storage
{
    // Worker-owned transaction: derive peaks from pending samples before they
    // are discarded, without rereading the written audio blocks.
    class AudioSourceBuilder
    {
    public:
        struct Options
        {
            std::function<bool()> cancel;
            std::shared_ptr<const waveform::SourcePeaks> cachedPeaks;
            std::shared_ptr<waveform::ProgressivePeaks> progressivePeaks;
            std::shared_ptr<ImportAudioReader> progressiveAudio;
            std::function<void()> onBlockPublished;
        };

    private:
        AudioShape shape;
        Options options;
        std::unique_ptr<waveform::StreamingPeakBuilder> peaks;
        AudioRevisionBuilder audio;

        void checkCanceled() const
        {
            if (options.cancel && options.cancel())
            {
                throw LongTaskCanceledError{};
            }
        }

        void publishBlock(
            int64_t first,
            std::span<const AudioRevisionBuilder::PendingChannel> channels,
            uint32_t count)
        {
            checkCanceled();
            if (peaks)
            {
                peaks->appendFrom(
                    shape, first + count,
                    [&](int channel, int64_t start, std::span<float> output)
                    {
                        if (channel < 0 || std::size_t(channel) >= channels.size() ||
                            start < first || start - first > count ||
                            output.size() > count - (start - first))
                        {
                            throw std::logic_error(
                                "Waveform requested samples outside pending block");
                        }
                        std::copy_n(channels[channel].data() + start - first,
                                    output.size(), output.data());
                    });
            }
            if (options.onBlockPublished)
            {
                options.onBlockPublished();
            }
        }

    public:
        AudioSourceBuilder(AudioShape shape,
                           std::shared_ptr<AudioBlockStore> store,
                           std::shared_ptr<DecodedBlockCache> cache,
                           Options options = {})
            : shape(shape), options(std::move(options)),
              peaks(this->options.cachedPeaks ? nullptr
                  : std::make_unique<waveform::StreamingPeakBuilder>(
                        shape, cache, this->options.cancel,
                        this->options.progressivePeaks)),
              audio(shape, std::move(store), std::move(cache),
                    [this](int64_t first, auto channels, uint32_t count)
                    { publishBlock(first, channels, count); },
                    this->options.progressiveAudio)
        {
            checkCanceled();
        }

        // The audio callback refers to this transaction.
        AudioSourceBuilder(const AudioSourceBuilder &) = delete;
        AudioSourceBuilder &operator=(const AudioSourceBuilder &) = delete;
        AudioSourceBuilder(AudioSourceBuilder &&) = delete;
        AudioSourceBuilder &operator=(AudioSourceBuilder &&) = delete;

        void appendInterleaved(std::span<const float> samples)
        {
            audio.appendInterleaved(samples);
        }

        void appendChannelMetadata(
            int channel, int64_t first,
            std::span<const cupuacu::audio::SampleProvenance> provenance,
            std::span<const uint8_t> dirty)
        {
            audio.appendChannelMetadata(channel, first, provenance, dirty);
        }

        std::shared_ptr<const AudioRevision>
        finish(std::filesystem::path ownedSource = {},
               uint64_t preservationSourceId = 0)
        {
            checkCanceled();
            auto summaries = peaks ? peaks->finish() : options.cachedPeaks;
            return audio.finish(std::move(ownedSource), std::move(summaries),
                                preservationSourceId);
        }
    };
}
