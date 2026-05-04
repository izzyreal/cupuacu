#pragma once

#include "SelectedChannels.hpp"
#include "AudioBuffer.hpp"

#include <cstdint>
#include <memory>
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
    class AudioProcessor;

    struct Play
    {
        cupuacu::Document *document;
        std::shared_ptr<cupuacu::audio::AudioBuffer> bufferSnapshot;
        uint8_t channelCountSnapshot = 0;
        uint64_t startPos;
        uint64_t endPos;
        bool loopEnabled;
        bool selectionIsActive;
        SelectedChannels selectedChannels;
        gui::VuMeter *vuMeter;
        std::shared_ptr<const AudioProcessor> previewProcessor;
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
        uint8_t channelCountSnapshot = 0;
        uint64_t startPos;
        uint64_t endPos;
        bool boundedToEnd;
        gui::VuMeter *vuMeter;
    };

    using AudioMessage = std::variant<Play, Stop, Record, UpdatePlayback>;
} // namespace cupuacu::audio
