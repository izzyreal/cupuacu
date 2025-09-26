#pragma once
#include "Component.h"
#include "../CupuacuState.h"
#include <SDL3/SDL.h>

#include "SamplePoint.h"

class Waveform : public Component {

public:
    static bool shouldShowSamplePoints(const double samplesPerPixel, const uint8_t pixelScale);

    static uint16_t getWaveformWidth(CupuacuState *state)
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
            waveform->setDirtyRecursive();
        }
    }

    Waveform(CupuacuState*, const uint8_t channelIndex);

    void onDraw(SDL_Renderer*) override;
    void timerCallback() override;
    void resized() override;
    void mouseLeave() override;
    void updateSamplePoints();
    void clearHighlight();

    std::optional<size_t> getSamplePosUnderCursor() const;
    void setSamplePosUnderCursor(const size_t samplePosUnderCursor);
    void resetSamplePosUnderCursor();

private:
    std::optional<size_t> lastDrawnSamplePosUnderCursor;
    std::optional<size_t> samplePosUnderCursor;
    const uint8_t channelIndex;
    double playbackPosition = 0;

    std::vector<std::unique_ptr<SamplePoint>> computeSamplePoints();
    
    void drawHorizontalLines(SDL_Renderer*);
    void drawSelection(SDL_Renderer*);
    void drawHighlight(SDL_Renderer*);
    void renderBlockWaveform(SDL_Renderer*);
    void renderSmoothWaveform(SDL_Renderer*);

    void drawPlaybackPosition(SDL_Renderer*);
};
