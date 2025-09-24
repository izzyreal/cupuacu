#pragma once
#include "Component.h"
#include "../CupuacuState.h"
#include <SDL3/SDL.h>

#include "SamplePoint.h"

class Waveform : public Component {

public:
    static bool shouldShowSamplePoints(const double samplesPerPixel, const uint8_t hardwarePixelsPerAppPixel);

    static float sampleIndexToXPosition(float sampleIndex, double sampleOffset, double samplesPerPixel, bool isSamplePointsVisible) {
        if (isSamplePointsVisible) {
            sampleIndex -= 0.5f;
        }
        return (sampleIndex - sampleOffset) / samplesPerPixel;
    }

    static float xPositionToSampleIndex(float xPos, double sampleOffset, double samplesPerPixel, bool isSamplePointsVisible, const size_t frameCount) {
        float sampleIndex = (xPos * samplesPerPixel) + sampleOffset;
        if (isSamplePointsVisible) {
            sampleIndex += 0.5f;
        }
        return std::min((float)frameCount, sampleIndex);
    }
    
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
    void resized() override;
    void mouseLeave() override;
    void updateSamplePoints();
    void clearHighlight();

    int64_t samplePosUnderCursor;

private:
    const uint8_t channelIndex;
    double playbackPosition = 0;

    std::vector<std::unique_ptr<SamplePoint>> computeSamplePoints();
    
    void drawHorizontalLines(SDL_Renderer*);
    void drawSelection(SDL_Renderer*);
    void drawHighlight(SDL_Renderer*);
    void renderBlockWaveform(SDL_Renderer*);
    void renderSmoothWaveform(SDL_Renderer*);

    void drawPlaybackPosition(SDL_Renderer*);
    void updateSamplePosUnderMouseCursor();
};
