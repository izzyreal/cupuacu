#pragma once

#include "Component.h"
#include "../readerwriterqueue.h"
#include "Helpers.h"
#include <cmath>
#include <algorithm>

using namespace moodycamel;

class VuMeter : public Component {
public:
    explicit VuMeter(CupuacuState* state)
        : Component(state, "VuMeter"),
          numChannels(1)
    {
        sampleQueues.resize(numChannels);
        previousPeaks.resize(numChannels, 0.f);
    }

    void setNumChannels(int n)
    {
        numChannels = n;
        sampleQueues.resize(numChannels);
        previousPeaks.resize(numChannels, 0.f);
    }

    void pushSampleForChannel(float sample, int channel)
    {
        if (channel < 0 || channel >= numChannels) return;
        sampleQueues[channel].try_enqueue(std::abs(sample));
    }

void onDraw(SDL_Renderer* renderer) override
{
    SDL_Rect fullBounds = getLocalBounds();
    Helpers::fillRect(renderer, fullBounds, Colors::black);

    int border = std::max(1, 2 / state->pixelScale);
    int barSpacing = fullBounds.h / numChannels;

    constexpr float attackTimeSec = 0.02f;
    constexpr float releaseTimeSec = 0.02f;
    float dt = 1.0f / 60.0f;
    float alphaAttack = 1.0f - std::exp(-dt / attackTimeSec);
    float alphaRelease = 1.0f - std::exp(-dt / releaseTimeSec);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float peak = 0.f;
        float val;

        while (sampleQueues[ch].try_dequeue(val))
        {
            float db = 20.f * log10f(std::max(val, 1e-5f));
            float normalized = (db + 72.f) / 72.f;
            normalized = std::clamp(normalized, 0.f, 1.f);
            normalized = std::pow(normalized, 0.5f);
            peak = std::max(peak, normalized);
        }

        if (peak > previousPeaks[ch])
            previousPeaks[ch] += (peak - previousPeaks[ch]) * alphaAttack;
        else
            previousPeaks[ch] += (peak - previousPeaks[ch]) * alphaRelease;

        SDL_Rect barRect = fullBounds;
        barRect.y += ch * barSpacing + border;
        barRect.h = barSpacing - 2 * border;
        barRect.w = fullBounds.w - 2 * border;
        int filledWidth = static_cast<int>(previousPeaks[ch] * barRect.w);

        for (int x = 0; x < filledWidth; ++x)
        {
            float frac = (float)x / barRect.w;
            SDL_Color color;

            if (frac < 0.86f)
            {
                float g = 128.f + 127.f * (frac / 0.86f);
                color = {0, (Uint8)g, 0, 255};
            }
            else if (frac < 0.95f)
            {
                float t = (frac - 0.86f) / (0.95f - 0.86f);
                color = {Uint8(255 * t), 255, 0, 255};
            }
            else
            {
                float t = (frac - 0.95f) / (1.0f - 0.95f);
                color = {255, Uint8(255 * (1.0f - t)), 0, 255};
            }

            SDL_Rect pixelRect {barRect.x + x, barRect.y, 1, barRect.h};
            Helpers::fillRect(renderer, pixelRect, color);
        }
    }
}

    void timerCallback() override
    {
        if (state->document.channels.size() > 0 && numChannels != state->document.channels.size())
        {
            setNumChannels(state->document.channels.size());
        }
        setDirty();
    }

private:
    int numChannels;
    std::vector<ReaderWriterQueue<float>> sampleQueues;
    std::vector<float> previousPeaks;
};

