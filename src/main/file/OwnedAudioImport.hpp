#pragma once

#include "file_loading.hpp"
#include "OwnedSourceFile.hpp"
#include "../storage/AudioRevision.hpp"
#include "../waveform/DecodedWaveformBuilder.hpp"
#include "../waveform/StreamingPeakBuilder.hpp"
#include "../waveform/ProgressivePeaks.hpp"
#include <fstream>
#ifdef __APPLE__
#include <sys/clonefile.h>
#endif

namespace cupuacu::file
{
    struct OwnedAudioImport
    {
        std::shared_ptr<const storage::AudioRevision> audio;
        LoadedAudioFile metadata;
        bool sourceCloned = false;
    };

    struct OwnedImportOptions
    {
        bool preferFilesystemClone = true;
        std::filesystem::path waveformCacheRoot;
        bool publishMetadata = false;
        bool publishAudio = false;
    };

    // Worker-only backend. The original bytes are retained independently
    // of the source path, including PCM32 precision not representable in float.
    inline OwnedAudioImport importOwnedAudio(
        const std::filesystem::path &source,
        const std::filesystem::path &workingDirectory,
        std::shared_ptr<storage::DecodedBlockCache> cache,
        const LoadProgressCallback &progress = {},
        const LoadCancelCheck &cancel = {},
        const std::function<void(waveform::DecodedWaveformChunk)> &preview = {},
        OwnedImportOptions options = {})
    {
        detail::throwIfLoadCanceled(cancel);
        if (!cache)
        {
            throw std::invalid_argument(
                "Audio import requires a decoded block cache");
        }
        auto store =
            std::make_shared<storage::AudioBlockStore>(workingDirectory);
        const auto owned =
            store->path() / ("source" + source.extension().string());
        const auto sourceBytes = std::filesystem::file_size(source);
        const auto sourceTime = std::filesystem::last_write_time(source);
        const bool cloned = cloneOrCopySource(
            source, owned,
            [&](double value)
            {
                detail::throwIfLoadCanceled(cancel);
                if (progress)
                {
                    progress("Copying source: " + source.filename().string(), value);
                }
            },
            options.preferFilesystemClone);
        detail::throwIfLoadCanceled(cancel);
        if (std::filesystem::file_size(source) != sourceBytes ||
            std::filesystem::last_write_time(source) != sourceTime)
        {
            throw std::runtime_error("Source changed during import");
        }

        std::unique_ptr<waveform::StreamingPeakBuilder> peaks;
        std::shared_ptr<waveform::ProgressivePeaks> progressivePeaks;
        std::shared_ptr<const waveform::SourcePeaks> sourcePeaks;
        bool cacheLoaded = false;
        std::unique_ptr<storage::AudioRevisionBuilder> builder;
        storage::AudioShape shape;
        std::shared_ptr<storage::ImportAudioReader> progressive;
        auto metadata = loadAudioFile(
            owned.string(),
            [&](const auto &, std::optional<double> value)
            {
                if (progress)
                    progress("Preparing audio: " + source.filename().string(), value);
            }, cancel, {},
            [&](const Document &document, int64_t start, const float *samples,
                int64_t count)
            {
                detail::throwIfLoadCanceled(cancel);
                if (!builder)
                {
                    shape = {document.getFrameCount(),
                             int(document.getChannelCount()),
                             document.getSampleRate(),
                             document.getSampleFormat()};
                    if (options.publishAudio)
                    {
                        progressive =
                            std::make_shared<storage::ImportAudioReader>(
                                shape, store, cache);
                    }
                    progressivePeaks =
                        std::make_shared<waveform::ProgressivePeaks>(shape,
                                                                     cache);
                    const auto publish = [&]
                    {
                        if (!preview || !options.publishMetadata)
                        {
                            return;
                        }
                        waveform::DecodedWaveformChunk chunk;
                        chunk.format = shape.format;
                        chunk.sampleRate = shape.sampleRate;
                        chunk.frameCount = shape.frames;
                        chunk.channels.resize(shape.channels);
                        chunk.audio = progressive;
                        chunk.progressivePeaks = progressivePeaks;
                        chunk.sourcePeaks = sourcePeaks;
                        preview(std::move(chunk));
                    };
                    publish();
                    sourcePeaks = waveform::loadPersistentSourcePeaks(
                        source.string(), document, options.waveformCacheRoot,
                        cache, cancel);
                    cacheLoaded = bool(sourcePeaks);
                    if (cacheLoaded)
                    {
                        publish();
                    }
                    else
                    {
                        peaks =
                            std::make_unique<waveform::StreamingPeakBuilder>(
                                shape, cache, cancel, progressivePeaks);
                    }
                    builder = std::make_unique<storage::AudioRevisionBuilder>(
                        shape, store, cache,
                        [&](int64_t blockStart,
                            std::span<const storage::AudioRevisionBuilder::
                                          PendingChannel>
                                channels,
                            uint32_t frames)
                        {
                            detail::throwIfLoadCanceled(cancel);
                            if (!cacheLoaded)
                            {
                                peaks->appendFrom(
                                    shape, blockStart + frames,
                                    [&](int channel, int64_t first,
                                        std::span<float> output)
                                    {
                                        if (first < blockStart ||
                                            first - blockStart + output.size() >
                                                frames)
                                        {
                                            throw std::logic_error(
                                                "Waveform requested samples "
                                                "outside decoded block");
                                        }
                                        std::copy_n(channels[channel].data() +
                                                        first - blockStart,
                                                    output.size(),
                                                    output.data());
                                    });
                            }
                            if (preview)
                            {
                                waveform::DecodedWaveformChunk chunk;
                                chunk.format = shape.format;
                                chunk.sampleRate = shape.sampleRate;
                                chunk.frameCount = shape.frames;
                                chunk.channels.resize(shape.channels);
                                chunk.audio = progressive;
                                chunk.progressivePeaks = progressivePeaks;
                                chunk.sourcePeaks = sourcePeaks;
                                preview(std::move(chunk));
                            }
                        },
                        progressive);
                }
                (void)start;
                builder->appendInterleaved(std::span<const float>(
                    samples, std::size_t(count) * shape.channels));
            });
        if (!builder)
        {
            shape = {metadata.document.getFrameCount(),
                     int(metadata.document.getChannelCount()),
                     metadata.document.getSampleRate(),
                     metadata.document.getSampleFormat()};
            builder = std::make_unique<storage::AudioRevisionBuilder>(
                shape, store, cache);
        }
        detail::throwIfLoadCanceled(cancel);
        metadata.persistentWaveformCacheChecked =
            !options.waveformCacheRoot.empty();
        metadata.persistentWaveformCacheLoaded = cacheLoaded;
        if (peaks)
        {
            sourcePeaks = peaks->finish();
        }
        if (!cacheLoaded)
        {
            metadata.pendingImportedPeaks =
                waveform::capturePersistentSourcePeaks(
                    source.string(), metadata.document,
                    options.waveformCacheRoot, sourcePeaks);
        }
        auto audio =
            builder->finish(owned, std::move(sourcePeaks),
                            metadata.document.getPreservationSourceId());
        metadata.waveformCachesReady = true;
        return {std::move(audio), std::move(metadata), cloned};
    }
} // namespace cupuacu::file
