#pragma once

#include "Component.h"
#include "Label.h"
#include <vector>
#include <string>

#include "text.h"

class Ruler : public Component {
public:
    explicit Ruler(CupuacuState* state)
        : Component(state, "Ruler")
    {}

    void setLabels(const std::vector<std::string>& newLabels)
    {
        labelsText = newLabels;

        for (auto* lbl : labels)
        {
            removeChild(lbl);
        }

        labels.clear();

        for (const auto& txt : labelsText)
        {
            auto* lbl = emplaceChild<Label>(state, txt);
            lbl->setMargin(0);
            lbl->setFontSize(18);
            lbl->setCenterHorizontally(true);
            labels.push_back(lbl);
        }
    }

    void setLongTickInterval(int interval) { longTickInterval = interval; }
    void setCenterFirstLabel(bool shouldCenter) { centerFirstLabel = shouldCenter; }
    void setBaseMargin(float margin) { baseMargin = margin; } // configurable margin
    void setScrollOffsetPx(int px) { scrollOffsetPx = px; }   // <-- new

    float getLabelAreaHeight() const { return baseLabelAreaHeight / state->pixelScale; }
    float getTickAreaHeight()  const { return baseTickAreaHeight / state->pixelScale; }

    void resized() override {
        SDL_Rect bounds = getLocalBounds();

        int numLabels = static_cast<int>(labelsText.size());
        if (numLabels == 0) return;

        int margin  = static_cast<int>(baseMargin / state->pixelScale);
        int spacing = (bounds.w - 2 * margin) / (numLabels - 1);

        int baseLabelHeight = static_cast<int>(baseLabelAreaHeight / state->pixelScale);
        int fixedLabelWidth = static_cast<int>(30 / state->pixelScale);
        int labelOffset     = static_cast<int>(15 / state->pixelScale);

        for (int i = 0; i < numLabels; ++i) {
            SDL_Rect labelRect;

            if (i == 0 && !centerFirstLabel) {
                int baseY = (int)(bounds.y + baseTickAreaHeight / state->pixelScale);
                auto [tw, th] = measureText(labelsText[i], labels[i]->getEffectiveFontSize());

                labelRect = {
                    0,
                    baseY,
                    tw,
                    baseLabelHeight
                };
            }
            else if (i == 0 /*|| i == numLabels - 1*/) {
                int tickX = bounds.x + margin + i * spacing - scrollOffsetPx;

                if (i == 0 && !centerFirstLabel) {
                    labelRect = {
                        tickX,
                        (int)(bounds.y + baseTickAreaHeight / state->pixelScale),
                        fixedLabelWidth,
                        baseLabelHeight
                    };
                } else {
                    labelRect = {
                        tickX - labelOffset,
                        (int)(bounds.y + baseTickAreaHeight / state->pixelScale),
                        fixedLabelWidth,
                        baseLabelHeight
                    };
                }
            } else {
                int tickX = bounds.x + margin + i * spacing - scrollOffsetPx;

                auto [tw, th] = measureText(labelsText[i], labels[i]->getEffectiveFontSize());
                if (tw <= 0) tw = spacing;

                labelRect = {
                    tickX - tw / 2,
                    (int)(bounds.y + baseTickAreaHeight / state->pixelScale),
                    tw,
                    baseLabelHeight
                };
            }

            labels[i]->setBounds(labelRect);
        }

        if (!centerFirstLabel)
        {
            if (Helpers::intersects(labels[0]->getBounds(), labels[1]->getBounds()))
            {
                labels[1]->setVisible(false);
            }

            auto lastLabel = labels[labels.size() - 1];
            auto lastLabelBounds = lastLabel->getBounds();
            lastLabel->setBounds(getWidth() - lastLabel->getWidth(), lastLabel->getYPos(), lastLabel->getWidth(), lastLabel->getHeight());
        }
    }

    void onDraw(SDL_Renderer* renderer) override {
        Helpers::fillRect(renderer, getLocalBounds(), Colors::background);
        SDL_Rect bounds = getLocalBounds();

        int numLabels = static_cast<int>(labelsText.size());
        if (numLabels == 0) return;

        int margin  = static_cast<int>(baseMargin / state->pixelScale);
        int spacing = (bounds.w - 2 * margin) / (numLabels - 1);

        int tickHeightLong  = std::max(1.f, 14.f / state->pixelScale);
        int tickHeightShort = std::max(1.f, 3.f / state->pixelScale);

        for (int i = 0; i <= numLabels; ++i) {
            int tickStartX = bounds.x + margin + i * spacing - scrollOffsetPx;
            int numTicks   = (i < numLabels) ? longTickInterval : 1;

            for (int t = 0; t < numTicks; ++t) {
                int tickX   = tickStartX + t * (spacing / numTicks);
                int tickY   = bounds.y;
                int height  = (t == 0 && i < numLabels) ? tickHeightLong : tickHeightShort;

                SDL_Rect tickRect { tickX, tickY, 1, height };
                Helpers::fillRect(renderer, tickRect, Colors::white);
            }
        }
    }

private:
    std::vector<std::string> labelsText;
    std::vector<Label*> labels;
    int longTickInterval = 1;
    bool centerFirstLabel = true;

    float baseLabelAreaHeight = 30;
    float baseTickAreaHeight  = 8;
    float baseMargin          = 20;

    int scrollOffsetPx = 0; // <-- new
};

