#pragma once

#include "../DocumentSession.hpp"
#include "AudioExport.hpp"
#include <functional>
#include <optional>

namespace cupuacu { class Paths; }
namespace cupuacu::file
{
    struct LoadedAudioFile
    {
        Document document;
        std::optional<AudioExportSettings> exportSettings;
        waveform::DocumentWaveformCaches waveformCaches;
        bool persistentWaveformCacheChecked = false;
        bool persistentWaveformCacheLoaded = false;
        bool waveformCachesReady = false;
        bool requiresSaveAs = false;
        bool externalSamples = false;
        bool decodedAudioCacheLoaded = false;
        std::shared_ptr<const waveform::PersistentCacheSnapshot>
            pendingImportedPeaks;
        std::shared_ptr<const storage::AudioEditRevision> audioRevision;
        std::shared_ptr<const storage::AudioRevision> ownedSource;
    };

    using LoadProgressCallback =
        std::function<void(const std::string &, std::optional<double>)>;
    using LoadCancelCheck = std::function<bool()>;
    // A sink first receives metadata with nullptr/zero samples, then consumes
    // decoded chunks synchronously. Commit requires an attached audio revision.
    using LoadSampleSink =
        std::function<void(const Document &, int64_t, const float *, int64_t)>;

    // Worker-only decoding. A required sink receives metadata followed by
    // bounded sample blocks; the returned document contains shape and markers.
    LoadedAudioFile decodeAudioFile(
        const std::string &path, const LoadSampleSink &sink,
        const LoadProgressCallback &progress = {},
        const LoadCancelCheck &isCanceled = {});

    // Application installation accepts only owned revisions.
    void commitLoadedAudioFile(DocumentSession &, const std::string &path,
                               LoadedAudioFile, const Paths *paths = nullptr);

    namespace detail
    {
        void throwIfLoadCanceled(const LoadCancelCheck &);
    }
}
