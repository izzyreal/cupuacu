#pragma once

#include "Document.hpp"
#include "file/AudioExport.hpp"
#include "file/OverwritePreservationState.hpp"
#include "gui/Selection.hpp"

#include <algorithm>
#include <cstdint>
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
        gui::Selection<double> selection = gui::Selection<double>(0.0);
        int64_t cursor = 0;

        void clearCurrentFile()
        {
            currentFile.clear();
            currentFileExportSettings.reset();
            preservationReferenceFile.clear();
            preservationReferenceExportSettings.reset();
            overwritePreservation = {};
            overwritePreservationBrokenByOperation = false;
            overwritePreservationBrokenReason.clear();
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
    };
} // namespace cupuacu
