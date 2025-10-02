#pragma once

#include "Component.h"
#include "Waveform.h"

#include "Ruler.h"
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>

class Timeline : public Component {
public:
    enum class Mode { Decimal, Samples };

    explicit Timeline(CupuacuState* state)
        : Component(state, "Timeline")
    {
        ruler = emplaceChild<Ruler>(state, getComponentName());
        ruler->setCenterFirstLabel(false);
        ruler->setLongTickSubdivisions(5);
        setMode(Mode::Samples);

        lastSamplesPerPixel = state->samplesPerPixel;
    }

    void setMode(Mode m)
    {
        mode = m;
        ruler->setMandatoryEndLabel("smpl");
        updateLabels();
    }

    void resized() override
    {
        ruler->setBounds(getLocalBounds());
        updateLabels();
    }

    void onDraw(SDL_Renderer* renderer) override
    {
        Helpers::fillRect(renderer, getLocalBounds(), Colors::background);
    }

    void timerCallback() override
    {
        if (state->samplesPerPixel != lastSamplesPerPixel ||
            state->sampleOffset != lastSampleOffset) {
            lastSamplesPerPixel = state->samplesPerPixel;
            lastSampleOffset = state->sampleOffset;
            updateLabels();
        }
    }

    int computeSamplesPerTick(double samplesPerPixel, int waveformWidth)
    {
        double targetPxPerTick = waveformWidth / 20.0;
        double rawSamples = targetPxPerTick * samplesPerPixel;

        double basePower = std::pow(10.0, std::floor(std::log10(rawSamples)));

        int multiplier;
        if (rawSamples / basePower <= 1.5)
            multiplier = 1;
        else if (rawSamples / basePower <= 3.5)
            multiplier = 2;
        else
            multiplier = 5;

        return std::max(1, int(basePower * multiplier));
    }

    void updateLabels()
    {
        if (getWidth() <= 0)
            return;

        const int waveformWidth = Waveform::getWaveformWidth(state);

        bool highZoom = Waveform::shouldShowSamplePoints(state->samplesPerPixel, state->pixelScale);

        int samplesPerTick = highZoom ? 1 : computeSamplesPerTick(state->samplesPerPixel, waveformWidth);

        int firstSampleWithTick = highZoom ? state->sampleOffset
                                                 : ((state->sampleOffset + samplesPerTick - 1) / samplesPerTick) * samplesPerTick;

        firstSampleWithTick = std::max(0, firstSampleWithTick - samplesPerTick);

        const int lastVisibleSample = Waveform::getSampleIndexForXPos(waveformWidth - 1, state->sampleOffset, state->samplesPerPixel);

        int lastSampleWithTick = highZoom ? lastVisibleSample
                                                : ((lastVisibleSample + samplesPerTick - 1) / samplesPerTick) * samplesPerTick;

        const float firstTickX = highZoom
            ? Waveform::getXPosForSampleIndex(firstSampleWithTick, state->sampleOffset, state->samplesPerPixel)
            : Waveform::getDoubleXPosForSampleIndex(firstSampleWithTick, state->sampleOffset, state->samplesPerPixel);

        const float lastTickX = highZoom
            ? Waveform::getXPosForSampleIndex(lastSampleWithTick, state->sampleOffset, state->samplesPerPixel)
            : Waveform::getDoubleXPosForSampleIndex(lastSampleWithTick, state->sampleOffset, state->samplesPerPixel);

        const int visibleTickCount = (lastSampleWithTick - firstSampleWithTick) / samplesPerTick;
        float tickSpacingPx = visibleTickCount > 0 ? (lastTickX - firstTickX) / visibleTickCount : samplesPerTick;

        ruler->setLongTickSpacingPx(tickSpacingPx);

        std::vector<std::string> labels {"smpl"};

        for (int samplePos = firstSampleWithTick + samplesPerTick; samplePos <= lastSampleWithTick; samplePos += samplesPerTick)
        {
            std::string text;

            if (mode == Mode::Samples)
            {
                text = std::to_string(samplePos);
            }
            else
            {
                double seconds = double(samplePos) / state->document.sampleRate;
                int mm = int(seconds / 60);
                double ss = seconds - mm * 60;
                std::ostringstream oss;
                oss << mm << ":" << std::fixed << std::setprecision(3) << ss;
                text = oss.str();
            }

            labels.push_back(text);
        }

        ruler->setLabels(labels);
        ruler->setScrollOffsetPx(firstTickX);
        ruler->resized();
    }

private:
    Ruler* ruler;
    Mode mode = Mode::Decimal;

    double lastSamplesPerPixel = 0.0;
    int64_t lastSampleOffset = 0;
};

