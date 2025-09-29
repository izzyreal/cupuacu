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
    bool mouseDown(const MouseEvent&) override;
    bool mouseUp(const MouseEvent&) override;
    bool mouseMove(const MouseEvent&) override;
    void onDraw(SDL_Renderer*) override;
};

