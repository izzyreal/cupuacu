#pragma once

#include "Component.h"
#include "LabeledField.h"
#include "text.h"
#include "../CupuacuState.h"

#include <SDL3/SDL.h>
#include <string>

class StatusBar : public Component {
private:
    LabeledField* startField = nullptr;
    LabeledField* endField = nullptr;
    LabeledField* posField = nullptr;

public:
    StatusBar(CupuacuState* stateToUse)
        : Component(stateToUse, "StatusBar")
    {
        startField = emplaceChildAndSetDirty<LabeledField>(state, "St", "");
        endField = emplaceChildAndSetDirty<LabeledField>(state, "End", "");
        posField = emplaceChildAndSetDirty<LabeledField>(state, "Pos", "");
    }

    void resized() override
    {
        // Set fixed positions for all fields when the StatusBar is resized
        startField->setBounds(0, 0, 120, getHeight());
        endField->setBounds(120, 0, 120, getHeight());
        posField->setBounds(240, 0, 120, getHeight());
    }

    void onDraw(SDL_Renderer* renderer) override
    {
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_FRect r{ 0, 0, (float)getWidth(), (float)getHeight() };
        SDL_RenderFillRect(renderer, &r);

        if (state->selectionStartSample != state->selectionEndSample)
        {
            const auto finalStart = state->selectionStartSample < state->selectionEndSample ?
                (int)std::floor(state->selectionStartSample) : (int)std::floor(state->selectionEndSample);
            const auto finalEnd = state->selectionStartSample < state->selectionEndSample ?
                (int)std::floor(state->selectionEndSample) : (int)std::floor(state->selectionStartSample);
            startField->setValue(std::to_string(finalStart));
            endField->setValue(std::to_string(finalEnd));
        }
        else
        {
            startField->setValue("");
            endField->setValue("");
        }

        posField->setValue(std::to_string((int)state->playbackPosition.load()));
    }

    void timerCallback() override
    {
        setDirtyRecursive();
    }
};
