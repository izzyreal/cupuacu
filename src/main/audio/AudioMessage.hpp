#pragma once

#include "SelectedChannels.hpp"

#include <variant>

namespace cupuacu::gui
{
    class VuMeter;
}

namespace cupuacu
{
    class Document;
}

namespace cupuacu::audio
{
    struct Play
    {
        cupuacu::Document *document;
        uint64_t startPos;
        uint64_t endPos;
        bool selectionIsActive;
        SelectedChannels selectedChannels;
        gui::VuMeter *vuMeter;
    };

    struct Stop
    {
    };

    struct Record
    {
        cupuacu::Document *document;
        uint64_t startPos;
        gui::VuMeter *vuMeter;
    };

    using AudioMessage = std::variant<Play, Stop, Record>;
} // namespace cupuacu::audio
