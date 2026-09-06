#include "DocumentSession.hpp"
#include "concurrency/DeferredRelease.hpp"
#include "storage/AudioEditRevision.hpp"
#include "storage/DocumentAudioReader.hpp"
#include "waveform/WaveformViewport.hpp"

namespace cupuacu
{
    std::shared_ptr<const storage::AudioReader>
    DocumentSession::getAudioReader() const
    {
        if (readRevision)
        {
            if (readRevisionVersion != document.getWaveformDataVersion())
            {
                throw std::logic_error(
                    "Session metadata changed without its audio revision");
            }
            return readRevision;
        }
        return std::make_shared<storage::DocumentAudioReader>(document);
    }

    std::shared_ptr<const storage::AudioEditRevision>
    DocumentSession::getEditRevision() const
    {
        if (readRevision &&
            readRevisionVersion != document.getWaveformDataVersion())
        {
            throw std::logic_error("Session revision is stale");
        }
        return readRevision;
    }

    bool DocumentSession::commitEditRevision(
        const std::shared_ptr<const storage::AudioEditRevision> &expected,
        std::shared_ptr<const storage::AudioEditRevision> replacement,
        std::vector<DocumentMarker> markers)
    {
        if (!expected || getEditRevision() != expected || !replacement)
        {
            return false;
        }
        const auto shape = replacement->shape();
        const auto old = expected->shape();
        if (shape.channels != old.channels ||
            shape.sampleRate != old.sampleRate || shape.format != old.format)
        {
            throw std::invalid_argument("Edit changes document audio format");
        }
        auto retained = concurrency::releaseOnWorker(std::move(replacement));
        document.setExternalAudioShape(shape.format, shape.sampleRate,
                                       shape.channels, shape.frames);
        document.replaceMarkers(std::move(markers));
        readRevision = std::move(retained);
        readRevisionVersion = document.getWaveformDataVersion();
        clearPendingPersistentWaveformCacheSave();
        viewportSource.reset();
        return true;
    }

    bool DocumentSession::revisionHasUnsavedChanges() const
    {
        return readRevision && (readRevision != savedReadRevision ||
                                document.getMarkers() != savedRevisionMarkers);
    }
    void DocumentSession::markRevisionSaved(
        std::shared_ptr<const storage::AudioEditRevision> revision,
        std::vector<DocumentMarker> markers)
    {
        savedReadRevision = concurrency::releaseOnWorker(std::move(revision));
        savedRevisionMarkers = std::move(markers);
    }
    void DocumentSession::clearReadRevision()
    {
        recoveredRevisionCheckpoint.reset();
        readRevision.reset();
        savedReadRevision.reset();
        savedRevisionMarkers.clear();
        preservationSource.reset();
        viewportSource.reset();
        viewportSourceVersion = UINT64_MAX;
        viewportBufferIdentity = nullptr;
    }
    void DocumentSession::bindReadRevision(
        std::shared_ptr<const storage::AudioEditRevision> revision)
    {
        if (!revision || revision->shape().frames != document.getFrameCount() ||
            revision->shape().channels != document.getChannelCount() ||
            revision->shape().sampleRate != document.getSampleRate() ||
            revision->shape().format != document.getSampleFormat())
        {
            throw std::invalid_argument(
                "Read revision does not match session metadata");
        }
        auto retained = concurrency::releaseOnWorker(std::move(revision));
        const auto shape = retained->shape();
        document.setExternalAudioShape(shape.format, shape.sampleRate,
                                       shape.channels, shape.frames);
        clearPendingPersistentWaveformCacheSave();
        readRevision = std::move(retained);
        savedReadRevision = readRevision;
        savedRevisionMarkers = document.getMarkers();
        preservationSource.reset();
        if (shape.frames)
        {
            readRevision->visitSourceRanges(
                0, 0, shape.frames,
                [&](const auto &range)
                {
                    if (!preservationSource && range.source &&
                        !range.source->sourcePath().empty())
                    {
                        preservationSource =
                            concurrency::releaseOnWorker(range.source);
                    }
                });
        }

        readRevisionVersion = document.getWaveformDataVersion();
        viewportSource.reset();
    }
    std::shared_ptr<const waveform::ViewportSource>
    DocumentSession::getViewportSource() const
    {
        if (document.getFrameCount() <= 0 || openingPreview)
        {
            return {};
        }
        const auto version = document.getWaveformDataVersion();
        const auto identity =
            readRevision ? nullptr : document.getAudioBuffer().get();
        if (readRevision && readRevisionVersion != version)
        {
            throw std::logic_error(
                "Session metadata changed without committing its read "
                "revision");
        }
        if (!readRevision && getWaveformCacheBuildProgress())
        {
            return {};
        }
        if (viewportSource && viewportSourceVersion == version &&
            viewportBufferIdentity == identity)
        {
            return viewportSource;
        }
        waveform::ViewportSource source;
        if (readRevision)
        {
            source.audio = readRevision;
            source.prepare =
                [revision = readRevision](const std::function<bool()> &cancel)
            {
                storage::AudioEditRevision::PeakWork work;
                return revision->prepareWaveform(work, cancel);
            };
            source.overview = [revision = readRevision](
                                  int channel, int64_t start, int64_t count)
            {
                storage::AudioEditRevision::PeakWork work;
                return revision->queryWaveformOverview(channel, start, count,
                                                       work);
            };
        }
        else
        {
            if (waveformCaches.getChannelCount() != document.getChannelCount())
            {
                return {};
            }
            source.audio =
                std::make_shared<storage::DocumentAudioReader>(document);
            std::vector<std::vector<gui::PeakLevel>> levels;
            for (int c = 0; c < document.getChannelCount(); ++c)
            {
                const auto &cache = getWaveformCache(c);
                if (cache.hasDirtyBlocks() ||
                    cache.getLevelByIndex(0).size() !=
                        std::size_t(document.getFrameCount() / 128 +
                                    (document.getFrameCount() % 128 != 0)))
                {
                    return {};
                }
                levels.push_back(cache.snapshotBuildState().levels);
            }
            auto peaks = std::make_shared<waveform::SourcePeaks>(
                source.audio->shape(), std::move(levels));
            source.overview =
                [peaks](int channel, int64_t start,
                        int64_t count) -> std::optional<waveform::Peak>
            {
                uint64_t visited = 0;
                const auto end = start + count;
                return peaks->queryBlocks(channel, start / 128,
                                          end / 128 + (end % 128 != 0),
                                          visited);
            };
        }
        viewportSource = concurrency::releaseOnWorker(
            std::make_shared<waveform::ViewportSource>(std::move(source)));
        viewportSourceVersion = version;
        viewportBufferIdentity = identity;
        return viewportSource;
    }
} // namespace cupuacu
