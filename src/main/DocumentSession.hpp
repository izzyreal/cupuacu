#pragma once

#include "Document.hpp"
#include "gui/Selection.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

namespace cupuacu
{
    struct DocumentSession
    {
        std::string currentFile =
            "/Users/izmar/Documents/VMPC2000XL/Volumes/MPC2000XL.bk2/BOAT.WAV";
        Document document;
        gui::Selection<double> selection = gui::Selection<double>(0.0);
        int64_t cursor = 0;

        void syncSelectionAndCursorToDocumentLength()
        {
            const int64_t frameCount = document.getFrameCount();
            selection.setHighest(frameCount);
            cursor = std::clamp(cursor, int64_t{0}, frameCount);
        }
    };
} // namespace cupuacu
