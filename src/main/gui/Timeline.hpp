#pragma once

#include "Component.hpp"
#include "Waveform.hpp"

#include "Ruler.hpp"
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>

namespace cupuacu::gui
{

    class Timeline : public Component
    {
    public:
        enum class Mode
        {
            Decimal,
            Samples
        };

        explicit Timeline(State *state) : Component(state, "Timeline")
        {
            ruler = emplaceChild<Ruler>(state, getComponentName());
            ruler->setCenterFirstLabel(false);
            setMode(Mode::Samples);

            lastSamplesPerPixel = state->samplesPerPixel;
        }

        void setMode(const Mode m)
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

        void onDraw(SDL_Renderer *renderer) override
        {
            Helpers::fillRect(renderer, getLocalBounds(), Colors::background);
        }

        void timerCallback() override
        {
            if (state->samplesPerPixel != lastSamplesPerPixel ||
                state->sampleOffset != lastSampleOffset)
            {
                lastSamplesPerPixel = state->samplesPerPixel;
                lastSampleOffset = state->sampleOffset;
                updateLabels();
            }
        }

        void updateLabels() const
        {
            if (getWidth() <= 0)
            {
                return;
            }

            const int waveformWidth = Waveform::getWaveformWidth(state);

            double maxTicks = waveformWidth * state->pixelScale / 85.f;
            maxTicks = std::max(1.0, maxTicks);

            const int totalVisibleSamples =
                Waveform::getSampleIndexForXPos(waveformWidth - 1,
                                                state->sampleOffset,
                                                state->samplesPerPixel) -
                state->sampleOffset;

            const int rawSamplesPerTick = std::max(
                1, int(std::ceil(double(totalVisibleSamples) / maxTicks)));

            static const std::vector<int> niceSteps = {
                1,       2,         5,         10,        20,        50,
                100,     200,       500,       1000,      2000,      5000,
                10'000,  20'000,    50'000,    100'000,   200'000,   400'000,
                800'000, 1'600'000, 3'200'000, 6'400'000, 12'800'000};

            int samplesPerTick = niceSteps.back();
            for (const int step : niceSteps)
            {
                if (step >= rawSamplesPerTick)
                {
                    samplesPerTick = step;
                    break;
                }
            }

            const bool highZoom = Waveform::shouldShowSamplePoints(
                state->samplesPerPixel, state->pixelScale);

            int firstSampleWithTick =
                (state->sampleOffset + samplesPerTick - 1) / samplesPerTick *
                samplesPerTick;
            firstSampleWithTick =
                std::max(0, firstSampleWithTick - samplesPerTick);

            const int lastVisibleSample = Waveform::getSampleIndexForXPos(
                waveformWidth - 1, state->sampleOffset, state->samplesPerPixel);

            const int lastSampleWithTick =
                (lastVisibleSample + samplesPerTick - 1) / samplesPerTick *
                samplesPerTick;

            const float firstTickX =
                highZoom
                    ? Waveform::getXPosForSampleIndex(firstSampleWithTick,
                                                      state->sampleOffset,
                                                      state->samplesPerPixel)
                    : Waveform::getDoubleXPosForSampleIndex(
                          firstSampleWithTick, state->sampleOffset,
                          state->samplesPerPixel);

            const float lastTickX =
                highZoom
                    ? Waveform::getXPosForSampleIndex(lastSampleWithTick,
                                                      state->sampleOffset,
                                                      state->samplesPerPixel)
                    : Waveform::getDoubleXPosForSampleIndex(
                          lastSampleWithTick, state->sampleOffset,
                          state->samplesPerPixel);

            const int visibleTickCount =
                (lastSampleWithTick - firstSampleWithTick) / samplesPerTick;
            const float tickSpacingPx =
                visibleTickCount > 0
                    ? (lastTickX - firstTickX) / visibleTickCount
                    : samplesPerTick;

            ruler->setLongTickSpacingPx(tickSpacingPx);

            std::vector<std::string> labels{"smpl"};

            for (int samplePos = firstSampleWithTick + samplesPerTick;
                 samplePos <= lastSampleWithTick; samplePos += samplesPerTick)
            {
                std::string text;

                if (mode == Mode::Samples)
                {
                    text = std::to_string(samplePos);
                }
                else
                {
                    const double seconds =
                        double(samplePos) / state->document.getSampleRate();
                    const int mm = int(seconds / 60);
                    const double ss = seconds - mm * 60;
                    std::ostringstream oss;
                    oss << mm << ":" << std::fixed << std::setprecision(3)
                        << ss;
                    text = oss.str();
                }

                labels.push_back(text);
            }

            int subdivisions;
            if (state->samplesPerPixel < 1 / 200.0f)
            {
                subdivisions = 1;
            }
            else
            {
                int temp = samplesPerTick;
                while (temp >= 10)
                {
                    temp /= 10;
                }
                subdivisions = temp == 2 ? 2 : 5;
            }

            ruler->setLongTickSubdivisions(subdivisions);
            ruler->setLabels(labels);
            ruler->setScrollOffsetPx(firstTickX);
            ruler->resized();
        }

    private:
        Ruler *ruler;
        Mode mode = Mode::Decimal;

        double lastSamplesPerPixel = 0.0;
        int64_t lastSampleOffset = 0;
    };
} // namespace cupuacu::gui
