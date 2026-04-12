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
        file::OverwritePreservationState overwritePreservation;
        Document document;
        gui::Selection<double> selection = gui::Selection<double>(0.0);
        int64_t cursor = 0;

        void clearCurrentFile()
        {
            currentFile.clear();
            currentFileExportSettings.reset();
            overwritePreservation = {};
        }

        void setCurrentFile(
            std::string pathToUse,
            std::optional<file::AudioExportSettings> settings = std::nullopt)
        {
            currentFile = std::move(pathToUse);
            currentFileExportSettings = std::move(settings);
            overwritePreservation = {};
        }

        void syncSelectionAndCursorToDocumentLength()
        {
            const int64_t frameCount = document.getFrameCount();
            selection.setHighest(frameCount);
            cursor = std::clamp(cursor, int64_t{0}, frameCount);
        }
    };
} // namespace cupuacu
