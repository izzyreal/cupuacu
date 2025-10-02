#pragma once

#include "Component.h"
#include "Label.h"
#include <vector>
#include <string>

class Ruler : public Component {
public:
    explicit Ruler(CupuacuState* state)
        : Component(state, "Ruler")
    {}

    void setLabels(const std::vector<std::string>& newLabels) {
        labelsText = newLabels;
        labels.clear();
        for (const auto& txt : labelsText) {
            auto* lbl = emplaceChild<Label>(state, txt);
            lbl->setMargin(0);
            lbl->setFontSize(18);
            lbl->setCenterHorizontally(true);
            labels.push_back(lbl);
        }
    }

    void setLongTickInterval(int interval) {
        longTickInterval = interval;
    }

    float getLabelAreaHeight() const { return baseLabelAreaHeight / state->pixelScale; }
    float getTickAreaHeight()  const { return baseTickAreaHeight / state->pixelScale; }

    void resized() override {
        SDL_Rect bounds = getLocalBounds();

        int numLabels = static_cast<int>(labelsText.size());
        if (numLabels == 0) return;

        int margin = static_cast<int>(baseMargin / state->pixelScale);
        int spacing = (bounds.w - 2 * margin) / (numLabels - 1);

        int labelWidth = static_cast<int>(30 / state->pixelScale);
        int labelOffset = static_cast<int>(15 / state->pixelScale);

        for (int i = 0; i < numLabels; ++i) {
            SDL_Rect labelRect {
                bounds.x + margin + i * spacing - labelOffset,
                (int)(bounds.y + baseTickAreaHeight / state->pixelScale),
                labelWidth,
                (int)(baseLabelAreaHeight / state->pixelScale)
            };
            labels[i]->setBounds(labelRect);
        }
    }

    void onDraw(SDL_Renderer* renderer) override {
        SDL_Rect bounds = getLocalBounds();

        int numLabels = static_cast<int>(labelsText.size());
        if (numLabels == 0) return;

        int margin = static_cast<int>(baseMargin / state->pixelScale);
        int spacing = (bounds.w - 2 * margin) / (numLabels - 1);

        int tickHeightLong = std::max(1.f, 10.f / state->pixelScale);
        int tickHeightShort = std::max(1.f, 3.f / state->pixelScale);

        for (int i = 0; i < numLabels; ++i) {
            int tickStartX = bounds.x + margin + i * spacing;
            int numTicks = (i < numLabels - 1) ? longTickInterval : 1;

            for (int t = 0; t < numTicks; ++t) {
                int tickX = tickStartX + t * (spacing / numTicks);
                int tickY = bounds.y;
                int height = (t == 0 && i < numLabels) ? tickHeightLong : tickHeightShort;

                SDL_Rect tickRect { tickX, tickY, 1, height };
                Helpers::fillRect(renderer, tickRect, Colors::white);
            }
        }
    }

private:
    std::vector<std::string> labelsText;
    std::vector<Label*> labels;
    int longTickInterval = 1;

    float baseLabelAreaHeight = 30;
    float baseTickAreaHeight  = 5;
    float baseMargin          = 20;
};
