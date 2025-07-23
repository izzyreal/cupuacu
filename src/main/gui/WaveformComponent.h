#pragma once
#include "Component.h"
#include "../CupuacuState.h"
#include <SDL3/SDL.h>

#include "SamplePoint.h"

struct WaveformComponent : Component {
    const static uint8_t LEFT_MARGIN = 0;
    const static uint8_t RIGHT_MARGIN = 0;
    const static uint8_t TOP_MARGIN = 80;
    const static uint8_t BOTTOM_MARGIN = 0;
    CupuacuState* state = nullptr;
    WaveformComponent(SDL_Rect r, CupuacuState*s) { rect = r; state = s; }
    void onDraw(SDL_Renderer*) override;
    bool onHandleEvent(const SDL_Event&) override;
    void timerCallback() override;

    bool shouldShowSamplePoints(const double samplesPerPixel, const uint8_t hardwarePixelsPerAppPixel);

    std::vector<std::unique_ptr<SamplePoint>> computeSamplePoints(int width, int height,
                                 const std::vector<int16_t>& samples, size_t offset,
                                 float samplesPerPixel, float verticalZoom, const uint8_t hardwarePixelsPerAppPixel);

    private:
    void renderSmoothWaveform(SDL_Renderer* renderer, int width, int height,
                                     const std::vector<int16_t>& samples, size_t offset,
                                     float samplesPerPixel, float verticalZoom, const uint8_t hardwarePixelsPerAppPixel);
    void handleScroll(const SDL_Event&);
    void handleDoubleClick();
    void startSelection(const SDL_Event&);
    void endSelection(const SDL_Event&);
};
