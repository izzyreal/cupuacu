#pragma once

#include "gui/Component.hpp"
#include "gui/text.hpp"
#include "gui/Helpers.hpp"
#include "gui/UiScale.hpp"

#include "State.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace cupuacu::gui
{
    class LabeledField : public Component
    {
    private:
        std::string label;
        std::string value;
        const SDL_Color background;

    public:
        LabeledField(State *stateToUse, const std::string &labelToUse,
                     const SDL_Color backgroundToUse)
            : Component(stateToUse, "LabeledField for " + labelToUse),
              label(labelToUse), background(backgroundToUse)
        {
        }

        void setValue(const std::string &newValue)
        {
            if (value != newValue)
            {
                value = newValue;
                setDirty();
            }
        }

        const std::string &getValue() const
        {
            return value;
        }

        void onDraw(SDL_Renderer *renderer) override
        {
            Helpers::fillRect(renderer, getLocalBounds(), background);
            const uint8_t fontPointSize =
                scaleFontPointSize(state, state->menuFontSize);
            const std::string displayText = label + ": " + value;
            const auto [textW, textH] = measureText(displayText, fontPointSize);
            auto rect = getLocalBoundsF();
            const float inset = std::max(
                0.0f, std::round((rect.h - static_cast<float>(textH)) / 2.0f));
            rect.x += inset;
            rect.y += inset;
            rect.w = std::max(0.0f, rect.w - (inset * 2.0f));
            rect.h = std::max(0.0f, rect.h - (inset * 2.0f));
            renderText(renderer, displayText, fontPointSize, rect, false);
        }
    };
} // namespace cupuacu::gui
