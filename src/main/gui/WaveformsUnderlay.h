#pragma once

#include "Component.h"

namespace cupuacu::gui {

class WaveformsUnderlay : public Component {
public:
    WaveformsUnderlay(cupuacu::State*);

    void mouseLeave() override;

    bool mouseDown(const MouseEvent&) override;

    bool mouseMove(const MouseEvent&) override;

    bool mouseUp(const MouseEvent&) override;

    void timerCallback() override;

private:
    uint8_t lastNumClicks = 0;

    uint16_t channelHeight() const;

    uint8_t channelAt(const uint16_t y) const;

    void markAllWaveformsDirty();

    void handleScroll(const int32_t mouseX, const int32_t mouseY);

    void handleChannelSelection(const int32_t mouseY, const bool isMouseDownEvent) const;
};
}

