#pragma once

#include "Colors.hpp"
#include "Component.hpp"
#include "Helpers.hpp"
#include "Label.hpp"
#include "UiScale.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace cupuacu::gui
{
    class VerticalRuler final : public Component
    {
    public:
        struct Marker
        {
            double normalizedPosition = 0.0;
            std::string label;
            bool emphasized = false;

            bool operator==(const Marker &other) const = default;
        };

        explicit VerticalRuler(State *state, const std::string &parentName)
            : Component(state, "VerticalRuler for " + parentName)
        {
        }

        void setMarkers(const std::vector<Marker> &markersToUse)
        {
            if (markers == markersToUse)
            {
                return;
            }

            markers = markersToUse;
            for (auto *label : labels)
            {
                removeChild(label);
            }
            labels.clear();

            for (const auto &marker : markers)
            {
                auto *label = emplaceChild<Label>(state, marker.label);
                label->setFontSize(16);
                labels.push_back(label);
            }

            resized();
            setDirty();
        }

        void setVerticalInsets(const int topInsetToUse,
                               const int bottomInsetToUse)
        {
            const int clampedTop = std::max(0, topInsetToUse);
            const int clampedBottom = std::max(0, bottomInsetToUse);
            if (topInset == clampedTop && bottomInset == clampedBottom)
            {
                return;
            }

            topInset = clampedTop;
            bottomInset = clampedBottom;
            resized();
            setDirty();
        }

        int getPreferredWidth() const
        {
            int maxLabelWidth = 0;
            for (const auto &marker : markers)
            {
                const auto [labelWidth, labelHeight] =
                    measureText(marker.label, scaleFontPointSize(state, 16));
                maxLabelWidth =
                    std::max(maxLabelWidth, std::max(labelWidth, labelHeight));
            }

            return std::max(scaleUi(state, 48.0f),
                            maxLabelWidth + scaleUi(state, 22.0f));
        }

        float normalizedPositionToY(const double normalizedPosition) const
        {
            const float top = static_cast<float>(topInset);
            const float bottom = static_cast<float>(
                std::max(topInset, getHeight() - 1 - bottomInset));
            return bottom - static_cast<float>(
                                std::clamp(normalizedPosition, 0.0, 1.0)) *
                                std::max(0.0f, bottom - top);
        }

        void resized() override
        {
            const int tickLength = scaleUi(state, 8.0f);
            const int labelPadding = scaleUi(state, 6.0f);
            const int labelAreaWidth =
                std::max(1, getWidth() - tickLength - labelPadding);
            const int labelHeight = scaleUi(state, 20.0f);

            for (std::size_t index = 0; index < labels.size(); ++index)
            {
                const int y = static_cast<int>(std::lround(
                    normalizedPositionToY(markers[index].normalizedPosition)));
                const int labelY =
                    std::clamp(y - labelHeight / 2, 0,
                               std::max(0, getHeight() - labelHeight));
                labels[index]->setBounds(0, labelY, labelAreaWidth,
                                         labelHeight);
            }
        }

        void onDraw(SDL_Renderer *renderer) override
        {
            Helpers::fillRect(renderer, getLocalBounds(), Colors::background);
            if (markers.empty())
            {
                return;
            }

            const int tickLength = scaleUi(state, 8.0f);
            const int axisX = getWidth() - 1;
            SDL_SetRenderDrawColor(renderer, 96, 96, 96, 255);
            SDL_RenderLine(renderer, static_cast<float>(axisX), 0.0f,
                           static_cast<float>(axisX),
                           static_cast<float>(getHeight()));

            for (const auto &marker : markers)
            {
                const float y =
                    normalizedPositionToY(marker.normalizedPosition);
                const Uint8 shade = marker.emphasized ? 180 : 120;
                SDL_SetRenderDrawColor(renderer, shade, shade, shade, 255);
                SDL_RenderLine(renderer, static_cast<float>(axisX - tickLength),
                               y, static_cast<float>(axisX), y);
            }
        }

    private:
        std::vector<Marker> markers;
        std::vector<Label *> labels;
        int topInset = 0;
        int bottomInset = 0;
    };
} // namespace cupuacu::gui
