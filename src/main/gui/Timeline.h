#pragma once

#include "Component.h"
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
        ruler = emplaceChild<Ruler>(state);
        ruler->setCenterFirstLabel(false);
        ruler->setBaseMargin(0);
        setMode(Mode::Samples);

        lastSamplesPerPixel = state->samplesPerPixel;
    }

    void setMode(Mode m)
    {
        mode = m;
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

    void updateLabels()
    {
        int width = getLocalBounds().w;
        if (width <= 0) return;

        double samplesPerPixel = state->samplesPerPixel;
        int64_t sampleOffset   = state->sampleOffset;
        double sampleRate      = state->document.sampleRate;

        // heuristic: aim for ~100px between major ticks
        int targetPixelsPerTick = 100;
        int64_t samplesPerTick = static_cast<int64_t>(samplesPerPixel * targetPixelsPerTick);
        if (samplesPerTick <= 0) samplesPerTick = 1;

        std::vector<std::string> labels;

        int numTicks = width / targetPixelsPerTick + 2;
        int64_t startSample = (sampleOffset / samplesPerTick) * samplesPerTick;

        for (int i = 0; i < numTicks; ++i) {
            int64_t samplePos = startSample + i * samplesPerTick;
            std::string text;

            if (mode == Mode::Samples) {
                if (i == 0 || i == numTicks - 1) {
                    text = "smpl";
                } else {
                    text = std::to_string(samplePos);
                }
            } else {
                double seconds = double(samplePos) / sampleRate;
                int mm = int(seconds / 60);
                double ss = seconds - mm * 60;
                std::ostringstream oss;
                oss << mm << ":" << std::fixed << std::setprecision(3) << ss;
                text = oss.str();
            }
            labels.push_back(text);
        }

        ruler->setLabels(labels);
        ruler->setLongTickInterval(5);
        int64_t sampleRemainder = sampleOffset % samplesPerTick;
        int scrollOffsetPx = static_cast<int>(sampleRemainder / samplesPerPixel);

        ruler->setScrollOffsetPx(scrollOffsetPx);
        ruler->resized();
    }

private:
    Ruler* ruler;
    Mode mode = Mode::Decimal;

    double lastSamplesPerPixel = 0.0;
    int64_t lastSampleOffset = 0;
};

