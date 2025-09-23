#pragma once

#include "Component.h"
#include "LabeledField.h"
#include "../CupuacuState.h"

#include <SDL3/SDL.h>
#include <string>

class StatusBar : public Component {
private:
    LabeledField* posField = nullptr;
    LabeledField* startField = nullptr;
    LabeledField* endField = nullptr;
    LabeledField* lengthField = nullptr;

public:
    StatusBar(CupuacuState* stateToUse)
        : Component(stateToUse, "StatusBar")
    {
        posField = emplaceChildAndSetDirty<LabeledField>(state, "Pos", "");
        startField = emplaceChildAndSetDirty<LabeledField>(state, "St", "");
        endField = emplaceChildAndSetDirty<LabeledField>(state, "End", "");
        lengthField = emplaceChildAndSetDirty<LabeledField>(state, "Len", "");
    }

    void resized() override
    {
        float scale = 4.0f / state->hardwarePixelsPerAppPixel;
        int fieldWidth = int(120 * scale);
        int fieldHeight = int(getHeight() * scale);

        posField->setBounds(0, 0, fieldWidth, fieldHeight);
        startField->setBounds(fieldWidth, 0, fieldWidth, fieldHeight);
        endField->setBounds(2 * fieldWidth, 0, fieldWidth, fieldHeight);
        lengthField->setBounds(3 * fieldWidth, 0, fieldWidth, fieldHeight);
    }

    void onDraw(SDL_Renderer* renderer) override
    {
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_FRect r{ 0, 0, (float)getWidth(), (float)getHeight() };
        SDL_RenderFillRect(renderer, &r);

        posField->setValue(std::to_string((int)state->playbackPosition.load()));

        if (state->selection.isActive())
        {
            startField->setValue(std::to_string(state->selection.getStartFloorInt()));
            endField->setValue(std::to_string(state->selection.getEndFloorInt()));
            lengthField->setValue(std::to_string(state->selection.getLengthInt()));
        }
        else
        {
            startField->setValue("");
            endField->setValue("");
            lengthField->setValue("");
        }
    }

    void timerCallback() override
    {
        setDirtyRecursive();
    }
};
