#pragma once

#include "Document.hpp"
#include "file/AudioExport.hpp"
#include "file/OverwritePreservationState.hpp"
#include "gui/Selection.hpp"
#include "undo/UndoStore.hpp"
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
        std::filesystem::path autosaveSnapshotPath;
        uint64_t autosavedWaveformDataVersion = 0;
        uint64_t autosavedMarkerDataVersion = 0;

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

        bool pumpWaveformCacheWork()
        {
            return waveformCaches.pumpWork(document,
                                           document.getWaveformDataVersion());
        }

        [[nodiscard]] std::optional<WaveformCacheBuildProgress>
        getWaveformCacheBuildProgress() const
        {
            return waveformCaches.getBuildProgress(
                document, document.getWaveformDataVersion());
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
            clearAutosaveSnapshotReference();
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
            clearAutosaveSnapshotReference();
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
