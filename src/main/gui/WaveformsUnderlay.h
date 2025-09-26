#pragma once

#include "Component.h"

struct CupuacuState;

class WaveformsUnderlay : public Component {
private:
public:
    WaveformsUnderlay(CupuacuState*);

    void mouseLeave() override;

    bool mouseLeftButtonDown(const uint8_t numClicks,
                             const int32_t mouseX,
                             const int32_t mouseY) override;

    bool mouseMove(const int32_t mouseX,
                   const int32_t mouseY,
                   const float /*mouseRelY*/,
                   const bool leftButtonIsDown) override;

    bool mouseLeftButtonUp(const uint8_t numClicks,
                           const int32_t mouseX,
                           const int32_t mouseY) override;

    void timerCallback() override;

private:
    uint16_t channelHeight() const;

    uint8_t channelAt(const uint16_t y) const;

    void markAllWaveformsDirty();

    void handleScroll(const int32_t mouseX, const int32_t mouseY);
};

