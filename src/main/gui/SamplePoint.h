#pragma once

#include "Component.h"
#include "../CupuacuState.h"

class SamplePoint : public Component
{
private:
    const int64_t sampleIndex;
    const uint8_t channelIndex;
    bool isDragging = false;
    float dragYPos = 0.f;

    float getSampleValueForYPos(const int16_t y, const uint16_t h, const double v, const uint16_t samplePointSize);

public:
    SamplePoint(CupuacuState*, const uint8_t channelIndexToUse, const int64_t sampleIndexToUse);

    uint64_t getSampleIndex() const;
    float getSampleValue() const;

    void mouseEnter() override;
    void mouseLeave() override;
    bool mouseLeftButtonDown(const uint8_t numClicks, const int32_t mouseX, const int32_t mouseY) override;
    bool mouseLeftButtonUp(const uint8_t numClicks, const int32_t mouseX, const int32_t mouseY) override;
    bool mouseMove(const int32_t mouseX, const int32_t mouseY, const float mouseRelY, const bool leftButtonIsDown) override;
    void onDraw(SDL_Renderer*) override;
};
