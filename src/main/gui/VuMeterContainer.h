#pragma once

#include "Component.h"
#include "VuMeter.h"
#include "Label.h"
#include <vector>
#include <string>

class VuMeterContainer : public Component {
public:
    explicit VuMeterContainer(CupuacuState* state)
        : Component(state, "VuMeterContainer")
    {
        vuMeter = emplaceChild<VuMeter>(state);
        state->vuMeter = vuMeter;
        dbLabels = { "dB" };
        for (int db = -72; db <= 0; db += 3)
        {
            dbLabels.push_back(std::to_string(db));
        }

        for (const auto& txt : dbLabels)
        {
            labels.push_back(emplaceChild<Label>(state, txt));
            labels.back()->setMargin(0);
            labels.back()->setFontSize(18);
            labels.back()->setCenterHorizontally(true);
        }
    }

    void resized() override
    {
        auto labelAreaHeight = baseLabelAreaHeight / state->pixelScale;
        auto tickAreaHeight = baseTickAreaHeight / state->pixelScale;
        SDL_Rect bounds = getLocalBounds();
        int meterHeight = bounds.h - labelAreaHeight - tickAreaHeight;
        int margin = static_cast<int>(baseMargin / state->pixelScale);

        vuMeter->setBounds(bounds.x + margin, bounds.y, bounds.w - 2 * margin, meterHeight);

        int numLabels = dbLabels.size();
        int spacing = (bounds.w - 2 * margin) / (numLabels - 1);

        int labelWidth = static_cast<int>(30 / state->pixelScale);
        int labelOffset = static_cast<int>(15 / state->pixelScale);

        for (int i = 0; i < numLabels; ++i)
        {
            SDL_Rect labelRect {
                bounds.x + margin + i * spacing - labelOffset,
                (int) (bounds.y + meterHeight + tickAreaHeight),
                labelWidth,
                (int) labelAreaHeight
            };
            labels[i]->setBounds(labelRect);
        }
    }

    void onDraw(SDL_Renderer* renderer) override
    {
        Helpers::fillRect(renderer, getLocalBounds(), Colors::black);

        SDL_Rect bounds = getLocalBounds();
        auto labelAreaHeight = baseLabelAreaHeight / state->pixelScale;
        auto tickAreaHeight = baseTickAreaHeight / state->pixelScale;
        int meterHeight = bounds.h - labelAreaHeight - tickAreaHeight;
        int margin = static_cast<int>(baseMargin / state->pixelScale);

        int numLabels = dbLabels.size();
        int spacing = (bounds.w - 2 * margin) / (numLabels - 1);

        int tickHeightLong = std::max(1.f, 10.f / state->pixelScale);
        int tickHeightShort = std::max(1.f, 3.f / state->pixelScale);

        for (int i = 0; i < numLabels; ++i)
        {
            int tickStartX = bounds.x + margin + i * spacing;
            int numTicks = (i < numLabels - 1) ? 3 : 1;

            for (int t = 0; t < numTicks; ++t)
            {
                int tickX = tickStartX + t * (spacing / numTicks);
                int tickY = bounds.y + meterHeight;
                int height = (t == 0 && i < numLabels) ? tickHeightLong : tickHeightShort;

                SDL_Rect tickRect { tickX, tickY, 1, height };
                Helpers::fillRect(renderer, tickRect, Colors::white);
            }
        }
    }

private:
    VuMeter* vuMeter;
    std::vector<std::string> dbLabels;
    std::vector<Label*> labels;
    float baseLabelAreaHeight = 30;
    float baseTickAreaHeight = 5;
    float baseMargin = 20;
};

