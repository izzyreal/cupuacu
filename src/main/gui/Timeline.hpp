#pragma once

#include "Component.hpp"
#include "Waveform.hpp"
#include "TimelinePlanning.hpp"

#include "Ruler.hpp"

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
            const auto &viewState =
                state->mainDocumentSessionWindow->getViewState();
            ruler = emplaceChild<Ruler>(state, getComponentName());
            ruler->setCenterFirstLabel(false);
            setMode(Mode::Samples);

            lastSamplesPerPixel = viewState.samplesPerPixel;
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
            const auto &viewState =
                state->mainDocumentSessionWindow->getViewState();
            if (planTimelineNeedsRefresh(lastSamplesPerPixel, lastSampleOffset,
                                         viewState.samplesPerPixel,
                                         viewState.sampleOffset))
            {
                lastSamplesPerPixel = viewState.samplesPerPixel;
                lastSampleOffset = viewState.sampleOffset;
                updateLabels();
            }
        }

        void updateLabels() const
        {
            const auto &viewState =
                state->mainDocumentSessionWindow->getViewState();
            if (getWidth() <= 0)
            {
                return;
            }

            const int waveformWidth = Waveform::getWaveformWidth(state);
            const bool showSamplePoints = Waveform::shouldShowSamplePoints(
                viewState.samplesPerPixel, state->pixelScale);
            const auto plan = planTimelineRuler(
                waveformWidth, state->pixelScale, viewState.sampleOffset,
                viewState.samplesPerPixel,
                state->activeDocumentSession.document.getSampleRate(),
                mode == Mode::Samples ? TimelinePlanningMode::Samples
                                      : TimelinePlanningMode::Decimal,
                showSamplePoints);
            if (!plan.valid)
            {
                return;
            }

            ruler->setLongTickSpacingPx(plan.tickSpacingPx);
            ruler->setLongTickSubdivisions(plan.subdivisions);
            ruler->setLabels(plan.labels);
            ruler->setScrollOffsetPx(plan.scrollOffsetPx);
            ruler->resized();
        }

    private:
        Ruler *ruler;
        Mode mode = Mode::Decimal;

        double lastSamplesPerPixel = 0.0;
        int64_t lastSampleOffset = 0;
    };
} // namespace cupuacu::gui
