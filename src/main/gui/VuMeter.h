#pragma once

#include "Component.h"
#include "../third_party/readerwriterqueue/readerwriterqueue.h"
#include "Helpers.h"
#include <cmath>
#include <algorithm>

using namespace moodycamel;

namespace cupuacu::gui {

class VuMeter : public Component {
public:
    explicit VuMeter(cupuacu::State* state)
        : Component(state, "VuMeter"),
          numChannels(1)
    {
        setNumChannels(numChannels);
    }

    void startDecay()
    {
        isDecaying.store(true, std::memory_order_relaxed);
    }

    void setNumChannels(int n)
    {
        numChannels = n;
        peakQueues.resize(numChannels);

        for (int i = 0; i < numChannels; i++)
        {
            // capacity set once here; no allocations happen on audio thread
            peakQueues[i] = ReaderWriterQueue<float>(24000);
        }

        previousPeaks.resize(numChannels, 0.f);
        peakHolds.resize(numChannels, 0.f);
        holdFrames.resize(numChannels, 0);
    }

    void setPeaksPushed() { peaksPushed.store(true, std::memory_order_relaxed); }

    // Called from audio thread
    void pushPeakForChannel(float peak, int channel)
    {
        if (channel < 0 || channel >= numChannels)
            return;

        // enqueue the peak (no allocation)
        peakQueues[channel].try_enqueue(peak);
    }

    void onDraw(SDL_Renderer* renderer) override
    {
        SDL_Rect fullBounds = getLocalBounds();
        Helpers::fillRect(renderer, fullBounds, Colors::black);

        int border = std::max(1, 2 / state->pixelScale);
        int barSpacing = fullBounds.h / numChannels;

        float dt = 1.0f / 60.0f;
        float alphaAttack = 1.0f - std::exp(-dt / attackTimeSec);
        float alphaRelease = 1.0f - std::exp(-dt / releaseTimeSec);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float peak = 0.f;
            float val;

            while (peakQueues[ch].try_dequeue(val))
            {
                peak = std::max(peak, val);
            }

            if (peak > 0.f)
            {
                float db = 20.f * log10f(std::max(peak, 1e-5f));
                float normalized = (db + 72.f) / 72.f;
                normalized = std::clamp(normalized, 0.f, 1.f);
                normalized = std::pow(normalized, 0.92f);
                peak = normalized;
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
            if (peak > peakHolds[ch]) {
                peakHolds[ch] = peak;
                holdFrames[ch] = holdTimeFrames;
            } else if (holdFrames[ch] > 0) {
                --holdFrames[ch];
            } else {
                // decay the hold slowly
                peakHolds[ch] -= 0.2f;
                if (peakHolds[ch] < previousPeaks[ch])
                    peakHolds[ch] = previousPeaks[ch];
            }

            int holdX = static_cast<int>(peakHolds[ch] * barRect.w);
            SDL_Rect peakLine = { barRect.x + holdX, barRect.y, 1, barRect.h };
            Helpers::fillRect(renderer, peakLine, Colors::green);
        }
    }

    void timerCallback() override
    {
        if (state->document.getChannelCount() > 0 && numChannels != state->document.getChannelCount())
        {
            setNumChannels(state->document.getChannelCount());
        }

        if (isDecaying.load(std::memory_order_relaxed))
        {
            releaseTimeSec = decayReleaseTimeSec;
            bool allZero = true;
            for (const auto& p : previousPeaks)
            {
                if (p > 1e-6f)
                {
                    allZero = false;
                    break;
                }
            }
            if (allZero)
            {
                isDecaying.store(false, std::memory_order_relaxed);
            }
        }
        else
        {
            releaseTimeSec = normalReleaseTimeSec;
        }

        if (peaksPushed.load(std::memory_order_relaxed) || isDecaying.load(std::memory_order_relaxed))
        {
            peaksPushed.store(false, std::memory_order_relaxed);
            setDirty();
        }
    }

private:
    const float attackTimeSec = 0.012f;
    const float normalReleaseTimeSec = 0.09f;
    const float decayReleaseTimeSec = 0.02f;
    float releaseTimeSec = normalReleaseTimeSec;
    std::atomic<bool> peaksPushed {false};
    std::atomic<bool> isDecaying{false};
    int numChannels;
    std::vector<ReaderWriterQueue<float>> peakQueues;
    std::vector<float> previousPeaks;
    std::vector<float> peakHolds;
    std::vector<int> holdFrames;
    static constexpr int holdTimeFrames = 30;
};
}
