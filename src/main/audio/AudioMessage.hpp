#pragma once

#include "SelectedChannels.hpp"
#include "AudioBuffer.hpp"
#include "MonitorProtection.hpp"

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

namespace cupuacu::storage
{
    class AudioReader;
}

namespace cupuacu::audio
{
    class AudioProcessor;
    struct PreparedPlayback;

    struct Play
    {
        cupuacu::Document *document = nullptr;
        std::shared_ptr<cupuacu::audio::AudioBuffer> bufferSnapshot;
        uint8_t channelCountSnapshot = 0;
        uint64_t startPos;
        uint64_t endPos;
        bool loopEnabled;
        bool selectionIsActive;
        SelectedChannels selectedChannels;
        gui::VuMeter *vuMeter;
        std::shared_ptr<const AudioProcessor> previewProcessor;
        std::shared_ptr<const storage::AudioReader> readerSnapshot;
        PreparedPlayback *prepared =
            nullptr; // Internal borrowed callback handle.
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

    struct SetInputMonitoring
    {
        bool enabled = false;
        uint8_t inputChannelCount = 0;
        gui::VuMeter *vuMeter = nullptr;
    };

    struct SetFeedbackSuppressionMode
    {
        FeedbackSuppressionMode mode = FeedbackSuppressionMode::Standard;
    };

    struct Record
    {
        uint64_t generation = 0;
        cupuacu::Document *document;
        uint8_t channelCountSnapshot = 0;
        uint64_t startPos;
        uint64_t endPos;
        bool boundedToEnd;
        gui::VuMeter *vuMeter;
    };

    using AudioMessage =
        std::variant<Play, Stop, SetInputMonitoring, SetFeedbackSuppressionMode,
                     Record, UpdatePlayback>;
} // namespace cupuacu::audio
