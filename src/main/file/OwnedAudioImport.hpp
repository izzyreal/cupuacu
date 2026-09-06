#pragma once

#include "file_loading.hpp"
#include "OwnedSourceFile.hpp"
#include "../storage/AudioRevision.hpp"
#include "../waveform/DecodedWaveformBuilder.hpp"
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
                    progress("Copying source", value);
                }
            },
            options.preferFilesystemClone);
        detail::throwIfLoadCanceled(cancel);
        if (std::filesystem::file_size(source) != sourceBytes ||
            std::filesystem::last_write_time(source) != sourceTime)
        {
            throw std::runtime_error("Source changed during import");
        }

        waveform::DecodedWaveformBuilder peaks;
        waveform::DocumentWaveformCaches cached;
        bool cacheLoaded = false;
        std::unique_ptr<storage::AudioRevisionBuilder> builder;
        storage::AudioShape shape;
        auto metadata = loadAudioFile(
            owned.string(), progress, cancel, {},
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
                    if (!options.waveformCacheRoot.empty())
                    {
                        DocumentSession cacheSession;
                        cacheSession.document = document;
                        cacheSession.currentFile = source.string();
                        cacheLoaded = waveform::loadPersistentWaveformCache(
                            cacheSession, options.waveformCacheRoot);
                        if (cacheLoaded)
                        {
                            cached = std::move(cacheSession.waveformCaches);
                        }
                    }
                    if (preview && options.publishMetadata)
                    {
                        waveform::DecodedWaveformChunk chunk;
                        chunk.format = shape.format;
                        chunk.sampleRate = shape.sampleRate;
                        chunk.frameCount = shape.frames;
                        chunk.channels.resize(shape.channels);
                        if (cacheLoaded)
                        {
                            chunk.cached = cached;
                        }
                        preview(std::move(chunk));
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
                            if (cacheLoaded)
                            {
                                return;
                            }
                            auto chunk = peaks.appendFrom(
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
                                                output.size(), output.data());
                                });
                            if (preview && chunk)
                            {
                                preview(std::move(*chunk));
                            }
                        });
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
        metadata.waveformCaches =
            cacheLoaded ? std::move(cached) : peaks.takeCaches();
        metadata.persistentWaveformCacheChecked =
            !options.waveformCacheRoot.empty();
        metadata.persistentWaveformCacheLoaded = cacheLoaded;
        std::shared_ptr<const waveform::SourcePeaks> sourcePeaks;
        if (shape.frames)
        {
            std::vector<std::vector<gui::PeakLevel>> levels;
            for (int channel = 0; channel < shape.channels; ++channel)
            {
                levels.push_back(metadata.waveformCaches.getCache(channel)
                                     .snapshotBuildState()
                                     .levels);
            }
            sourcePeaks = std::make_shared<waveform::SourcePeaks>(
                shape, std::move(levels));
        }
        auto audio =
            builder->finish(owned, std::move(sourcePeaks),
                            metadata.document.getPreservationSourceId());
        metadata.waveformCachesReady = true;
        return {std::move(audio), std::move(metadata), cloned};
    }
} // namespace cupuacu::file
