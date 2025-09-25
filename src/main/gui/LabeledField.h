#pragma once

#include "Component.h"
#include "text.h"
#include "../CupuacuState.h"

#include <SDL3/SDL.h>
#include <string>

class LabeledField : public Component {
private:
    std::string label;
    std::string value;

public:
    LabeledField(CupuacuState* stateToUse, const std::string& labelToUse, const std::string& valueToUse)
        : Component(stateToUse, "LabeledField for " + labelToUse), label(labelToUse), value(valueToUse)
    {
    }

    void setValue(const std::string& newValue)
    {
        if (value != newValue) {
            value = newValue;
            setDirty();
        }
    }

    void onDraw(SDL_Renderer* renderer) override
    {
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_FRect r{ 0, 0, (float)getWidth(), (float)getHeight() };
        SDL_RenderFillRect(renderer, &r);

        const uint8_t fontPointSize = state->menuFontSize / state->pixelScale;
        std::string displayText = label + ": " + value;
        renderText(renderer, displayText, fontPointSize);
    }
};
