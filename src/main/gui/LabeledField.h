#pragma once

#include "gui/Component.h"
#include "gui/text.h"
#include "gui/Helpers.h"

#include "State.h"

#include <SDL3/SDL.h>

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

        void onDraw(SDL_Renderer *renderer) override
        {
            Helpers::fillRect(renderer, getLocalBounds(), background);
            const uint8_t fontPointSize =
                state->menuFontSize / state->pixelScale;
            const std::string displayText = label + ": " + value;
            const auto rect = getLocalBoundsF();
            renderText(renderer, displayText, fontPointSize, rect, false);
        }
    };
} // namespace cupuacu::gui
