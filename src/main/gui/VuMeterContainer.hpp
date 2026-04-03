#pragma once

#include "Component.hpp"
#include "UiScale.hpp"
#include "VuMeter.hpp"
#include "Ruler.hpp"
#include "VuMeterScale.hpp"

#include <optional>

namespace cupuacu::gui
{
    class VuMeterContainer : public Component
    {
    public:
        explicit VuMeterContainer(State *state)
            : Component(state, "VuMeterContainer")
        {
            vuMeter = emplaceChild<VuMeter>(state);
            ruler = emplaceChild<Ruler>(state, getComponentName());
            ruler->setCenterFirstLabel(false);
            ruler->setNoTicksAfterLastLabel(true);
            syncScaleFromState();
        }

        void syncScaleFromState()
        {
            if (!state)
            {
                return;
            }

            const auto scale = state->vuMeterScale;
            if (lastScale.has_value() && scale == *lastScale)
            {
                return;
            }

            lastScale = scale;
            const auto config = getVuMeterScaleConfig(scale);
            ruler->setMandatoryEndLabel(config.endLabel);
            ruler->setLabels(config.labels);
            ruler->setLongTickSubdivisions(config.longTickSubdivisions);
            setDirty();
            resized();
        }

        void resized() override
        {
            const SDL_Rect bounds = getLocalBounds();

            const float labelAreaHeight = ruler->getLabelAreaHeight();
            const float tickAreaHeight = ruler->getTickAreaHeight();
            const int meterHeight =
                bounds.h - (labelAreaHeight + tickAreaHeight);

            const int margin = scaleUi(state, 20.0f);

            vuMeter->setBounds(bounds.x + margin, bounds.y,
                               bounds.w - 2 * margin, meterHeight);

            const SDL_Rect rulerBounds{
                bounds.x + margin, (int)(bounds.y + meterHeight),
                bounds.w - 2 * margin, (int)(labelAreaHeight + tickAreaHeight)};

            const auto config =
                getVuMeterScaleConfig(state ? state->vuMeterScale
                                            : VuMeterScale::PeakDbfs);
            ruler->setLongTickSpacingPx(
                config.intervalCount > 0
                    ? rulerBounds.w / static_cast<float>(config.intervalCount)
                    : static_cast<float>(rulerBounds.w));
            ruler->setBounds(rulerBounds);
        }

        void timerCallback() override
        {
            syncScaleFromState();
        }

        void onDraw(SDL_Renderer *renderer) override
        {
            Helpers::fillRect(renderer, getLocalBounds(), Colors::background);
        }

    private:
        VuMeter *vuMeter;
        Ruler *ruler;
        std::optional<VuMeterScale> lastScale;
    };
} // namespace cupuacu::gui
