#pragma once

#include "../SelectedChannels.hpp"

#include <cstdint>
#include <optional>

namespace cupuacu::gui
{
    struct HoveredSampleInfo
    {
        float value = 0.0f;
        int64_t channel = 0;
        int64_t frame = 0;

        bool operator==(const HoveredSampleInfo &other) const
        {
            return value == other.value && channel == other.channel &&
                   frame == other.frame;
        }

        bool operator!=(const HoveredSampleInfo &other) const
        {
            return !(*this == other);
        }
    };

    struct EditorViewState
    {
        double samplesPerPixel = 1.0;
        double verticalZoom = 1.0;
        int64_t sampleOffset = 0;
        SelectedChannels selectedChannels = SelectedChannels::BOTH;
        SelectedChannels hoveringOverChannels = SelectedChannels::BOTH;
        double samplesToScroll = 0.0;
        std::optional<HoveredSampleInfo> sampleValueUnderMouseCursor;
    };
} // namespace cupuacu::gui
