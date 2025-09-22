#pragma once
#include "Component.h"
#include "../CupuacuState.h"
#include <SDL3/SDL.h>

#include "SamplePoint.h"

class Waveform : public Component {

public:
    static uint32_t getWaveformWidth(CupuacuState *state)
    {
        if (state->waveforms.empty())
        {
            return 0;
        }

        return state->waveforms[0]->getWidth();
    }

    static void updateAllSamplePoints(CupuacuState *state)
    {
        for (auto &waveform : state->waveforms)
        {
            waveform->updateSamplePoints();
        }
    }

    static void setAllWaveformsDirty(CupuacuState *state)
    {
        for (auto &waveform : state->waveforms)
        {
            waveform->setDirty();
        }
    }

    Waveform(CupuacuState *stateToUse, const uint8_t channelIndex);

    void onDraw(SDL_Renderer*) override;
    void timerCallback() override;

    void updateSamplePoints();

private:
    const uint8_t channelIndex;
    double playbackPosition = 0;
    uint8_t numClicksOfLastMouseDown = 0;

    bool shouldShowSamplePoints(const double samplesPerPixel, const uint8_t hardwarePixelsPerAppPixel);

    std::vector<std::unique_ptr<SamplePoint>> computeSamplePoints(int width, int height,
                                 const std::vector<float>& samples, size_t offset,
                                 float samplesPerPixel, float verticalZoom, const uint8_t hardwarePixelsPerAppPixel);
    
    void renderSmoothWaveform(SDL_Renderer* renderer, int width, int height,
                                     const std::vector<float>& samples, size_t offset,
                                     float samplesPerPixel, float verticalZoom, const uint8_t hardwarePixelsPerAppPixel);

    void drawPlaybackPosition(SDL_Renderer *renderer, const double sampleOffset, const double samplesPerPixel);
};
