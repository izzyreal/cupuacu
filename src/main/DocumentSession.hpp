#pragma once

#include "Document.hpp"
#include "Paths.hpp"
#include "file/AudioExport.hpp"
#include "file/OverwritePreservationState.hpp"
#include "gui/Selection.hpp"
#include "undo/UndoStore.hpp"
#include "waveform/WaveformCachePersistence.hpp"
#include "waveform/DocumentWaveformCaches.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace cupuacu
{
    struct DocumentSession
    {
        std::string currentFile;
        std::optional<file::AudioExportSettings> currentFileExportSettings;
        bool currentFileRequiresSaveAs = false;
        std::string preservationReferenceFile;
        std::optional<file::AudioExportSettings>
            preservationReferenceExportSettings;
        file::OverwritePreservationState overwritePreservation;
        bool overwritePreservationBrokenByOperation = false;
        std::string overwritePreservationBrokenReason;
        Document document;
        waveform::DocumentWaveformCaches waveformCaches;
        bool openingPreview = false;
        gui::Selection<double> selection = gui::Selection<double>(0.0);
        int64_t cursor = 0;
        undo::UndoStore undoStore;
        mutable bool loggedRestartUndoPersistenceSizeWarning = false;
        std::filesystem::path autosaveSnapshotPath;
        uint64_t autosavedWaveformDataVersion = 0;
        uint64_t autosavedMarkerDataVersion = 0;
        std::optional<uint64_t> pendingPersistentWaveformCacheVersion;

        using WaveformCacheBuildProgress =
            waveform::DocumentWaveformCaches::BuildProgress;

        gui::WaveformCache &getWaveformCache(const int channel)
        {
            return waveformCaches.getCache(channel);
        }

        const gui::WaveformCache &getWaveformCache(const int channel) const
        {
            return waveformCaches.getCache(channel);
        }

        void invalidateWaveformSamples(const int64_t startSample,
                                       const int64_t endSample)
        {
            waveformCaches.invalidateSamples(startSample, endSample);
        }

        void updateWaveformCache()
        {
            if (openingPreview)
            {
                return;
            }
            waveformCaches.update(document, document.getWaveformDataVersion());
        }

        void markPendingPersistentWaveformCacheSave()
        {
            pendingPersistentWaveformCacheVersion =
                document.getWaveformDataVersion();
        }

        void clearPendingPersistentWaveformCacheSave()
        {
            pendingPersistentWaveformCacheVersion.reset();
        }

        [[nodiscard]] bool pumpWaveformCacheWork(const Paths *paths = nullptr)
        {
            if (openingPreview)
            {
                return false;
            }
            const bool stateChanged = waveformCaches.pumpWork(
                document, document.getWaveformDataVersion());
            // A deferred retry must never publish edited peaks under the
            // original source file's cache key.
            if (pendingPersistentWaveformCacheVersion &&
                *pendingPersistentWaveformCacheVersion !=
                    document.getWaveformDataVersion())
            {
                clearPendingPersistentWaveformCacheSave();
            }
            if (pendingPersistentWaveformCacheVersion && paths &&
                !getWaveformCacheBuildProgress().has_value())
            {
                const auto result =
                    waveform::schedulePersistentWaveformCache(*this, *paths);
                if (result != waveform::CacheSaveScheduleResult::Busy)
                {
                    clearPendingPersistentWaveformCacheSave();
                }
            }
            return stateChanged;
        }

        [[nodiscard]] std::optional<WaveformCacheBuildProgress>
        getWaveformCacheBuildProgress() const
        {
            if (openingPreview)
            {
                return WaveformCacheBuildProgress{
                    getWaveformCache(0).builtSamplePrefixEnd() /
                        gui::WaveformCache::BASE_BLOCK_SIZE,
                    (document.getFrameCount() +
                     gui::WaveformCache::BASE_BLOCK_SIZE - 1) /
                        gui::WaveformCache::BASE_BLOCK_SIZE};
            }
            return waveformCaches.getBuildProgress(
                document, document.getWaveformDataVersion());
        }

        [[nodiscard]] std::optional<waveform::PersistentCacheKey>
        getPersistentWaveformCacheKey() const
        {
            return waveform::makePersistentCacheKey(currentFile, document);
        }

        [[nodiscard]] std::filesystem::path
        getPersistentWaveformCachePath(const Paths &paths) const
        {
            const auto key = getPersistentWaveformCacheKey();
            if (!key.has_value())
            {
                return {};
            }
            return key->cachePath(paths);
        }

        void rebuildWaveformCacheSynchronously()
        {
            waveformCaches.rebuildSynchronously(document);
        }

        void stopWaveformCacheBuild()
        {
            waveformCaches.stopBuild();
        }

        void clearCurrentFile()
        {
            currentFile.clear();
            currentFileExportSettings.reset();
            currentFileRequiresSaveAs = false;
            preservationReferenceFile.clear();
            preservationReferenceExportSettings.reset();
            overwritePreservation = {};
            overwritePreservationBrokenByOperation = false;
            overwritePreservationBrokenReason.clear();
            loggedRestartUndoPersistenceSizeWarning = false;
            clearAutosaveSnapshotReference();
            clearPendingPersistentWaveformCacheSave();
        }

        void setCurrentFile(
            std::string pathToUse,
            std::optional<file::AudioExportSettings> settings = std::nullopt)
        {
            currentFile = std::move(pathToUse);
            currentFileExportSettings = std::move(settings);
            currentFileRequiresSaveAs = false;
            preservationReferenceFile = currentFile;
            preservationReferenceExportSettings = currentFileExportSettings;
            overwritePreservation = {};
            overwritePreservationBrokenByOperation = false;
            overwritePreservationBrokenReason.clear();
            loggedRestartUndoPersistenceSizeWarning = false;
            clearAutosaveSnapshotReference();
            clearPendingPersistentWaveformCacheSave();
        }

        void setPreservationReference(
            std::string pathToUse,
            std::optional<file::AudioExportSettings> settings = std::nullopt)
        {
            preservationReferenceFile = std::move(pathToUse);
            preservationReferenceExportSettings = std::move(settings);
        }

        void breakOverwritePreservation(std::string reason)
        {
            overwritePreservationBrokenByOperation = true;
            overwritePreservationBrokenReason = std::move(reason);
        }

        void clearOverwritePreservationBreak()
        {
            overwritePreservationBrokenByOperation = false;
            overwritePreservationBrokenReason.clear();
        }

        void syncSelectionAndCursorToDocumentLength()
        {
            const int64_t frameCount = document.getFrameCount();
            selection.setHighest(frameCount);
            cursor = std::clamp(cursor, int64_t{0}, frameCount);
        }

        void clearAutosaveSnapshotReference()
        {
            autosaveSnapshotPath.clear();
            autosavedWaveformDataVersion = 0;
            autosavedMarkerDataVersion = 0;
        }
    };
} // namespace cupuacu
