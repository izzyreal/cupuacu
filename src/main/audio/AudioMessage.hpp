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

    using AudioMessage = std::variant<Play, Stop>;
} // namespace cupuacu::audio
