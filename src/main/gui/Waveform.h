#pragma once
#include "Component.h"
#include "../CupuacuState.h"
#include <SDL3/SDL.h>

#include "SamplePoint.h"

class Waveform : public Component {

public:
    static bool shouldShowSamplePoints(const double samplesPerPixel, const uint8_t pixelScale);

    static void clearHighlightIfNotChannel(CupuacuState *state, const uint8_t channelIndexNotToClear)
    {
        for (int64_t waveformChannel = 0; waveformChannel < state->waveforms.size(); ++waveformChannel)
        {
            if (waveformChannel == channelIndexNotToClear)
            {
                continue;
            }

            state->waveforms[waveformChannel]->clearHighlight();
        }
    }

    static uint16_t getWaveformWidth(const CupuacuState *state)
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

    static int32_t getXPosForSampleIndex(const int64_t sampleIndex, const int64_t sampleOffset, const double samplesPerPixel)
    {
        return static_cast<int32_t>(std::roundf((static_cast<double>(sampleIndex) - sampleOffset) / samplesPerPixel));
    }
    
    static double getDoubleSampleIndexForXPos(const float xPos, const int64_t sampleOffset, const double samplesPerPixel)
    {
        return static_cast<double>(xPos) * samplesPerPixel + static_cast<double>(sampleOffset);
    }

    static int64_t getSampleIndexForXPos(const float xPos, const int64_t sampleOffset, const double samplesPerPixel)
    {
        return static_cast<int64_t>(std::llround(getDoubleSampleIndexForXPos(xPos, sampleOffset, samplesPerPixel)));
    }

    Waveform(CupuacuState*, const uint8_t channelIndex);

    void onDraw(SDL_Renderer*) override;
    void timerCallback() override;
    void resized() override;
    void mouseLeave() override;
    void updateSamplePoints();
    void clearHighlight();
    uint8_t getChannelIndex() const;

    std::optional<int64_t> getSamplePosUnderCursor() const;
    void setSamplePosUnderCursor(const int64_t samplePosUnderCursor);
    void resetSamplePosUnderCursor();

private:
    const SDL_Color waveformColor{0, 185, 0, 255};
    const SDL_FColor waveformFColor{waveformColor.r / 255.f, waveformColor.g / 255.f, waveformColor.b / 255.f, waveformColor.a / 255.f };
    std::optional<int64_t> lastDrawnSamplePosUnderCursor = -1;
    std::optional<int64_t> samplePosUnderCursor;
    const uint8_t channelIndex;
    double playbackPosition = 0;

    std::vector<std::unique_ptr<SamplePoint>> computeSamplePoints();
    
    void drawHorizontalLines(SDL_Renderer*);
    void drawSelection(SDL_Renderer*);
    void drawHighlight(SDL_Renderer*);
    void renderBlockWaveform(SDL_Renderer*);
    void renderSmoothWaveform(SDL_Renderer*);

    void drawPlaybackPosition(SDL_Renderer*);
    void drawCursor(SDL_Renderer*);
};
