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
        std::string preservationReferenceFile;
        std::optional<file::AudioExportSettings>
            preservationReferenceExportSettings;
        file::OverwritePreservationState overwritePreservation;
        bool overwritePreservationBrokenByOperation = false;
        std::string overwritePreservationBrokenReason;
        Document document;
        waveform::DocumentWaveformCaches waveformCaches;
        gui::Selection<double> selection = gui::Selection<double>(0.0);
        int64_t cursor = 0;
        undo::UndoStore undoStore;
        mutable bool loggedRestartUndoPersistenceSizeWarning = false;
        std::filesystem::path autosaveSnapshotPath;
        uint64_t autosavedWaveformDataVersion = 0;
        uint64_t autosavedMarkerDataVersion = 0;
        bool pendingPersistentWaveformCacheSave = false;

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
            waveformCaches.update(document, document.getWaveformDataVersion());
        }

        void markPendingPersistentWaveformCacheSave()
        {
            pendingPersistentWaveformCacheSave = true;
        }

        void clearPendingPersistentWaveformCacheSave()
        {
            pendingPersistentWaveformCacheSave = false;
        }

        [[nodiscard]] bool pumpWaveformCacheWork(const Paths *paths = nullptr)
        {
            const bool stateChanged = waveformCaches.pumpWork(
                document, document.getWaveformDataVersion());
            if (pendingPersistentWaveformCacheSave && paths &&
                !getWaveformCacheBuildProgress().has_value())
            {
                (void)waveform::savePersistentWaveformCache(*this, *paths);
                pendingPersistentWaveformCacheSave = false;
            }
            return stateChanged;
        }

        [[nodiscard]] std::optional<WaveformCacheBuildProgress>
        getWaveformCacheBuildProgress() const
        {
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
