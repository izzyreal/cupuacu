#pragma once

#include "../SelectedChannels.hpp"

#include <cstdint>
#include <optional>

namespace cupuacu::gui
{
    struct EditorViewState
    {
        double samplesPerPixel = 1.0;
        double verticalZoom = 1.0;
        int64_t sampleOffset = 0;
        SelectedChannels selectedChannels = SelectedChannels::BOTH;
        SelectedChannels hoveringOverChannels = SelectedChannels::BOTH;
        double samplesToScroll = 0.0;
        std::optional<float> sampleValueUnderMouseCursor;
    };
} // namespace cupuacu::gui
