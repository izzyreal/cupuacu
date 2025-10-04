#pragma once

#include "Component.h"
#include "VuMeter.h"
#include "Ruler.h"
#include <vector>
#include <string>

namespace cupuacu::gui {
class VuMeterContainer : public Component {
public:
    explicit VuMeterContainer(cupuacu::State* state)
        : Component(state, "VuMeterContainer")
    {
        vuMeter = emplaceChild<VuMeter>(state);
        state->vuMeter = vuMeter;

        std::vector<std::string> dbLabels{"dB"};

        for (int db = -72; db < 0; db += 3)
        {
            dbLabels.push_back(std::to_string(db));
        }

        ruler = emplaceChild<Ruler>(state, getComponentName());
        ruler->setMandatoryEndLabel("0");
        ruler->setLabels(dbLabels);
        ruler->setLongTickSubdivisions(3.f);
        ruler->setCenterFirstLabel(false);
        ruler->setNoTicksAfterLastLabel(true);
    }

    void resized() override
    {
        SDL_Rect bounds = getLocalBounds();

        float labelAreaHeight = ruler->getLabelAreaHeight();
        float tickAreaHeight  = ruler->getTickAreaHeight();
        int meterHeight = bounds.h - (labelAreaHeight + tickAreaHeight);

        int margin = static_cast<int>(20 / state->pixelScale);

        vuMeter->setBounds(bounds.x + margin, bounds.y,
                           bounds.w - 2 * margin, meterHeight);

        SDL_Rect rulerBounds {
            bounds.x + margin,
            (int)(bounds.y + meterHeight),
            bounds.w - 2 * margin,
            (int)(labelAreaHeight + tickAreaHeight)
        };

        ruler->setLongTickSpacingPx(rulerBounds.w / 25.f);
        ruler->setBounds(rulerBounds);
    }

    void onDraw(SDL_Renderer* renderer) override
    {
        Helpers::fillRect(renderer, getLocalBounds(), Colors::black);
    }

private:
    VuMeter* vuMeter;
    Ruler* ruler;
};
}

