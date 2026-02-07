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
        bool loopEnabled;
        bool selectionIsActive;
        SelectedChannels selectedChannels;
        gui::VuMeter *vuMeter;
    };

    struct UpdatePlayback
    {
        uint64_t startPos;
        uint64_t endPos;
        bool loopEnabled;
        bool selectionIsActive;
        SelectedChannels selectedChannels;
    };

    struct Stop
    {
    };

    struct Record
    {
        cupuacu::Document *document;
        uint64_t startPos;
        uint64_t endPos;
        bool boundedToEnd;
        gui::VuMeter *vuMeter;
    };

    using AudioMessage = std::variant<Play, Stop, Record, UpdatePlayback>;
} // namespace cupuacu::audio
