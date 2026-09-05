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

    void DocumentSession::clearReadRevision()
    {
        readRevision.reset();
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
        clearPendingPersistentWaveformCacheSave();
        readRevision = concurrency::releaseOnWorker(std::move(revision));
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
        const auto identity = document.getAudioBuffer().get();
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
