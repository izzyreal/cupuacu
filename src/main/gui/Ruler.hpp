#pragma once

#include "gui/Component.hpp"
#include "gui/Label.hpp"
#include "gui/Helpers.hpp"
#include "gui/Colors.hpp"
#include "gui/text.hpp"

#include <vector>
#include <string>

namespace cupuacu::gui
{
    class Ruler final : public Component
    {
    public:
        explicit Ruler(State *state, const std::string &parentName)
            : Component(state, "Ruler for " + parentName)
        {
        }

        void setMandatoryEndLabel(const std::string &mandatoryEndLabelToUse)
        {
            mandatoryEndLabel = mandatoryEndLabelToUse;
            setDirty();
        }

        void setLabels(const std::vector<std::string> &labelTextsToUse)
        {
            if (labelTexts == labelTextsToUse)
            {
                return;
            }

            labelTexts = labelTextsToUse;

            for (auto *label : labels)
            {
                removeChild(label);
            }

            labels.clear();

            for (const auto &labelText : labelTexts)
            {
                auto *label = emplaceChild<Label>(state, labelText);
                label->setFontSize(18);
                label->setCenterHorizontally(true);
                labels.push_back(label);
            }

            if (!mandatoryEndLabel.empty())
            {
                auto *label = emplaceChild<Label>(state, mandatoryEndLabel);
                label->setFontSize(18);
                label->setCenterHorizontally(true);
                labels.push_back(label);
            }
        }

        void setLongTickSubdivisions(const float subdivisions)
        {
            longTickSubdivisions = subdivisions;
            setDirty();
        }

        void setCenterFirstLabel(const bool shouldCenter)
        {
            centerFirstLabel = shouldCenter;
        }

        void setHorizontalMargin(const float margin)
        {
            baseHorizontalMargin = margin;
        }

        void setScrollOffsetPx(const int px)
        {
            scrollOffsetPx = px;
        }

        float getLabelAreaHeight() const
        {
            return baseLabelAreaHeight / state->pixelScale;
        }

        float getTickAreaHeight() const
        {
            return baseTickAreaHeight / state->pixelScale;
        }

        float getHorizontalMargin() const
        {
            return baseHorizontalMargin / state->pixelScale;
        }

        void resized() override
        {
            int numLabels = static_cast<int>(labelTexts.size());

            if (numLabels == 0)
            {
                return;
            }

            if (!mandatoryEndLabel.empty())
            {
                numLabels++;
            }

            const int labelHeight =
                static_cast<int>(baseLabelAreaHeight / state->pixelScale);
            const int labelY = getTickAreaHeight();

            for (int i = 0; i < numLabels; ++i)
            {
                const auto labelText = mandatoryEndLabel.empty() ? labelTexts[i]
                                       : i == numLabels - 1 ? mandatoryEndLabel
                                                            : labelTexts[i];
                auto [labelWidth, th] =
                    measureText(labelText, labels[i]->getEffectiveFontSize());
                int labelX =
                    longTickSpacingPx * i + scrollOffsetPx - labelWidth * 0.5f;

                if (baseHorizontalMargin != 0.f)
                {
                    labelX += getHorizontalMargin();
                }

                SDL_Rect labelRect{labelX, labelY, labelWidth, labelHeight};

                if (i == 0 && !centerFirstLabel)
                {
                    labelRect.x = 0;
                }

                labels[i]->setBounds(labelRect);
                const bool labelIsVisible =
                    labelRect.x >= 0 && labelRect.x + labelRect.w < getWidth();
                labels[i]->setVisible(labelIsVisible);
            }

            if (!centerFirstLabel && labels.size() > 1)
            {
                if (Helpers::intersects(labels[0]->getBounds(),
                                        labels[1]->getBounds()))
                {
                    labels[1]->setVisible(false);
                }
            }

            if (!mandatoryEndLabel.empty())
            {
                const auto mandatoryLabel = labels.back();
                const auto labelBounds = mandatoryLabel->getBounds();
                mandatoryLabel->setBounds(
                    getWidth() - mandatoryLabel->getWidth(),
                    mandatoryLabel->getYPos(), mandatoryLabel->getWidth(),
                    mandatoryLabel->getHeight());
                mandatoryLabel->setVisible(true);

                int lastVisibleNonEndLabelIndex = -1;

                for (int labelIndex = labels.size() - 2; labelIndex >= 0;
                     --labelIndex)
                {
                    if (!labels[labelIndex]->isVisible())
                    {
                        lastVisibleNonEndLabelIndex = labelIndex;
                        break;
                    }
                }

                if (lastVisibleNonEndLabelIndex != -1 &&
                    lastVisibleNonEndLabelIndex > 1)
                {
                    const auto lastVisibleNonEndLabel =
                        labels[lastVisibleNonEndLabelIndex];
                    if (Helpers::intersects(lastVisibleNonEndLabel->getBounds(),
                                            mandatoryLabel->getBounds()))
                    {
                        lastVisibleNonEndLabel->setVisible(false);
                    }
                }
                if (lastVisibleNonEndLabelIndex != -1 &&
                    lastVisibleNonEndLabelIndex > 1 && labels.size() > 3)
                {
                    const auto lastVisibleNonEndLabel =
                        labels[lastVisibleNonEndLabelIndex - 1];
                    if (Helpers::intersects(lastVisibleNonEndLabel->getBounds(),
                                            mandatoryLabel->getBounds()))
                    {
                        lastVisibleNonEndLabel->setVisible(false);
                    }
                }
            }
        }

        void onDraw(SDL_Renderer *renderer) override
        {
            const SDL_Rect bounds = getLocalBounds();

            const int tickHeightLong = std::max(1.f, 14.f / state->pixelScale);
            const int tickHeightShort = std::max(1.f, 3.f / state->pixelScale);

            bool logFirstTick = true;

            const int numLongTicks =
                static_cast<int>(bounds.w / longTickSpacingPx) + 2;

            const int noTicksAfterXPos =
                noTicksAfterLastLabel ? labels.back()->getCenterX() : bounds.w;

            for (int i = -1; i < numLongTicks + 1; ++i)
            {
                int tickStartX = bounds.x +
                                 static_cast<int>(i * longTickSpacingPx) +
                                 scrollOffsetPx;

                if (baseHorizontalMargin != 0.f)
                {
                    tickStartX += getHorizontalMargin();
                }

                for (int t = 0; t < longTickSubdivisions; ++t)
                {
                    if (i == numLongTicks - 1 && t > 0 && noTicksAfterLastLabel)
                    {
                        break;
                    }
                    int tickX =
                        std::floor(tickStartX + t * (longTickSpacingPx /
                                                     longTickSubdivisions));
                    if (tickX < 0 || tickX > bounds.x + bounds.w ||
                        tickX > noTicksAfterXPos)
                    {
                        if (i == numLongTicks - 2 && t == 0 &&
                            noTicksAfterLastLabel)
                        {
                            tickX -= 1;
                        }
                        else
                        {
                            continue;
                        }
                    }

                    const int height =
                        t == 0 ? tickHeightLong : tickHeightShort;

                    const SDL_Rect tickRect{tickX, bounds.y, 1, height};
                    Helpers::fillRect(renderer, tickRect, Colors::white);
                }
            }
        }

        void setLongTickSpacingPx(const float spacing)
        {
            longTickSpacingPx = spacing;
            setDirty();
        }

        void setNoTicksAfterLastLabel(const bool noTicksAfterLastLabelToUse)
        {
            noTicksAfterLastLabel = noTicksAfterLastLabelToUse;
            setDirty();
        }

    private:
        std::vector<std::string> labelTexts;
        std::vector<Label *> labels;
        std::string mandatoryEndLabel;
        bool noTicksAfterLastLabel = false;
        float longTickSubdivisions = 1.f;
        bool centerFirstLabel = true;

        float baseLabelAreaHeight = 30;
        float baseTickAreaHeight = 8;
        float baseHorizontalMargin = 0.f;

        float scrollOffsetPx = 0;
        float longTickSpacingPx = 0.f;
    };
} // namespace cupuacu::gui
