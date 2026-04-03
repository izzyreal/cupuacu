#pragma once

#include "gui/Component.hpp"
#include "gui/Helpers.hpp"
#include "gui/Colors.hpp"
#include "gui/UiScale.hpp"
#include "gui/VuMeterModel.hpp"
#include "gui/VuMeterScale.hpp"
#include "audio/MeterFrame.hpp"

#include "State.hpp"

#include <readerwriterqueue.h>

#include <algorithm>

using namespace moodycamel;

namespace cupuacu::gui
{
    class VuMeter : public Component
    {
    public:
        explicit VuMeter(State *state)
            : Component(state, "VuMeter"), numChannels(1)
        {
            setNumChannels(numChannels);
        }

        void startDecay()
        {
            isDecaying.store(true, std::memory_order_relaxed);
        }

        void setNumChannels(const int n)
        {
            numChannels = n;
            peakQueues.resize(numChannels);

            for (int i = 0; i < numChannels; i++)
            {
                // capacity set once here; no allocations happen on audio thread
                peakQueues[i] = ReaderWriterQueue<audio::MeterFrame>(24000);
            }

            meterModel.setNumChannels(numChannels);
        }

        void setPeaksPushed()
        {
            peaksPushed.store(true, std::memory_order_relaxed);
        }

        // Called from audio thread
        void pushMeterFrameForChannel(const audio::MeterFrame &frame,
                                      const int channel)
        {
            if (channel < 0 || channel >= numChannels)
            {
                return;
            }

            // enqueue meter data without allocation
            peakQueues[channel].try_enqueue(frame);
        }

        void onDraw(SDL_Renderer *renderer) override
        {
            const SDL_Rect fullBounds = getLocalBounds();
            Helpers::fillRect(renderer, fullBounds, Colors::black);

            const int border = scaleUi(state, 2.0f);
            const int barSpacing = fullBounds.h / numChannels;
            const auto scale =
                state ? state->vuMeterScale : VuMeterScale::PeakDbfs;
            meterModel.setScale(scale);
            const auto scaleConfig = getVuMeterScaleConfig(scale);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float peak = 0.0f;
                float rms = 0.0f;
                audio::MeterFrame val;

                while (peakQueues[ch].try_dequeue(val))
                {
                    peak = std::max(peak, val.peak);
                    rms = std::max(rms, val.rms);
                }

                const auto display = meterModel.advanceChannel(
                    ch, {.peak = peak, .rms = rms},
                    isDecaying.load(std::memory_order_relaxed));

                SDL_Rect barRect = fullBounds;
                barRect.y += ch * barSpacing + border;
                barRect.h = barSpacing - 2 * border;
                barRect.w = fullBounds.w - 2 * border;
                const int filledWidth =
                    static_cast<int>(display.level * barRect.w);

                for (int x = 0; x < filledWidth; ++x)
                {
                    const float frac = (float)x / barRect.w;
                    const float dbAtPixel = scaleConfig.minDbfs +
                        frac * (scaleConfig.maxDbfs - scaleConfig.minDbfs);
                    SDL_Color color;

                    if (dbAtPixel < scaleConfig.warningDbfs)
                    {
                        const float greenSpan = std::max(
                            1e-5f, scaleConfig.warningDbfs - scaleConfig.minDbfs);
                        const float t = std::clamp(
                            (dbAtPixel - scaleConfig.minDbfs) / greenSpan, 0.0f,
                            1.0f);
                        const float g = 128.f + 127.f * t;
                        color = {0, (Uint8)g, 0, 255};
                    }
                    else if (dbAtPixel < scaleConfig.redlineDbfs)
                    {
                        const float yellowSpan = std::max(
                            1e-5f, scaleConfig.redlineDbfs -
                                       scaleConfig.warningDbfs);
                        const float t = std::clamp(
                            (dbAtPixel - scaleConfig.warningDbfs) / yellowSpan,
                            0.0f, 1.0f);
                        color = {Uint8(255 * t), 255, 0, 255};
                    }
                    else
                    {
                        const float redSpan = std::max(
                            1e-5f, scaleConfig.maxDbfs -
                                       scaleConfig.redlineDbfs);
                        const float t = std::clamp(
                            (dbAtPixel - scaleConfig.redlineDbfs) / redSpan,
                            0.0f, 1.0f);
                        color = {255, Uint8(255 * (1.0f - t)), 0, 255};
                    }

                    const SDL_Rect pixelRect{barRect.x + x, barRect.y, 1,
                                             barRect.h};
                    Helpers::fillRect(renderer, pixelRect, color);
                }
                const int holdX = std::clamp(
                    static_cast<int>(display.hold * barRect.w), 0,
                    std::max(0, barRect.w - 1));
                const SDL_Rect peakLine = {barRect.x + holdX, barRect.y, 1,
                                           barRect.h};
                Helpers::fillRect(renderer, peakLine, Colors::green);
            }
        }

        void timerCallback() override
        {
            if (state->getActiveDocumentSession().document.getChannelCount() > 0 &&
                numChannels !=
                    state->getActiveDocumentSession().document.getChannelCount())
            {
                setNumChannels(
                    state->getActiveDocumentSession().document.getChannelCount());
            }

            if (isDecaying.load(std::memory_order_relaxed))
            {
                if (meterModel.isAtRest())
                {
                    isDecaying.store(false, std::memory_order_relaxed);
                }
            }

            if (peaksPushed.load(std::memory_order_relaxed) ||
                isDecaying.load(std::memory_order_relaxed))
            {
                peaksPushed.store(false, std::memory_order_relaxed);
                setDirty();
            }
        }

    private:
        std::atomic<bool> peaksPushed{false};
        std::atomic<bool> isDecaying{false};
        int numChannels;
        std::vector<ReaderWriterQueue<audio::MeterFrame>> peakQueues;
        VuMeterModel meterModel;
    };
} // namespace cupuacu::gui
