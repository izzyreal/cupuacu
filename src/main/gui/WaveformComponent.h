#pragma once
#include "Component.h"
#include "../CupuacuState.h"
#include <SDL3/SDL.h>

#include "SamplePoint.h"

class WaveformComponent : public Component {

public:
    WaveformComponent(CupuacuState *stateToUse);

    void onDraw(SDL_Renderer*) override;
    bool mouseMove(const int32_t mouseX,
                   const int32_t mouseY,
                   const float mouseRelY,
                   const bool leftButtonIsDown) override;
    bool mouseLeftButtonDown(const uint8_t numClicks, const int32_t mouseX, const int32_t mouseY) override;
    bool mouseLeftButtonUp(const uint8_t numClicks, const int32_t mouseX, const int32_t mouseY) override;
    void timerCallback() override;

    void updateSamplePoints();

private:
    uint8_t numClicksOfLastMouseDown = 0;

    bool shouldShowSamplePoints(const double samplesPerPixel, const uint8_t hardwarePixelsPerAppPixel);

    std::vector<std::unique_ptr<SamplePoint>> computeSamplePoints(int width, int height,
                                 const std::vector<int16_t>& samples, size_t offset,
                                 float samplesPerPixel, float verticalZoom, const uint8_t hardwarePixelsPerAppPixel);
    
    void renderSmoothWaveform(SDL_Renderer* renderer, int width, int height,
                                     const std::vector<int16_t>& samples, size_t offset,
                                     float samplesPerPixel, float verticalZoom, const uint8_t hardwarePixelsPerAppPixel);

    void handleScroll(const int32_t mouseX,
                      const int32_t mouseY);
    void handleDoubleClick();
    void startSelection(const int32_t mouseX);
    void endSelection(const int32_t mouseX);
};
