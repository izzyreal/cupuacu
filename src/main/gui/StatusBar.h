#pragma once

#include "Component.h"
#include "Waveform.h"
#include "WaveformsOverlay.h"
#include "LabeledField.h"
#include "../CupuacuState.h"
#include "SamplePoint.h"

#include <SDL3/SDL.h>
#include <string>
#include <cmath>

class StatusBar : public Component {
private:
    LabeledField* posField = nullptr;
    LabeledField* startField = nullptr;
    LabeledField* endField = nullptr;
    LabeledField* lengthField = nullptr;
    LabeledField* valueField = nullptr;

    int lastPlaybackPosition = -1;
    int lastSelectionStart = -1;
    int lastSelectionEnd = -1;
    bool lastSelectionActive = false;

public:
    StatusBar(CupuacuState* stateToUse)
        : Component(stateToUse, "StatusBar")
    {
        posField = emplaceChildAndSetDirty<LabeledField>(state, "Pos", "");
        startField = emplaceChildAndSetDirty<LabeledField>(state, "St", "");
        endField = emplaceChildAndSetDirty<LabeledField>(state, "End", "");
        lengthField = emplaceChildAndSetDirty<LabeledField>(state, "Len", "");
        valueField = emplaceChildAndSetDirty<LabeledField>(state, "Val", "");
    }

    void resized() override
    {
        float scale = 4.0f / state->pixelScale;
        int fieldWidth = int(120 * scale);
        int fieldHeight = int(getHeight() * scale);

        posField->setBounds(0, 0, fieldWidth, fieldHeight);
        startField->setBounds(fieldWidth, 0, fieldWidth, fieldHeight);
        endField->setBounds(2 * fieldWidth, 0, fieldWidth, fieldHeight);
        lengthField->setBounds(3 * fieldWidth, 0, fieldWidth, fieldHeight);
        valueField->setBounds(4 * fieldWidth, 0, fieldWidth, fieldHeight);
    }

    void onDraw(SDL_Renderer* renderer) override
    {
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_FRect r{ 0, 0, (float)getWidth(), (float)getHeight() };
        SDL_RenderFillRect(renderer, &r);
    }

    void timerCallback() override
    {
        const int currentPlaybackPosition = (int)state->playbackPosition.load();

        if (currentPlaybackPosition != lastPlaybackPosition)
        {
            lastPlaybackPosition = currentPlaybackPosition;
            posField->setValue(std::to_string(currentPlaybackPosition));
        }

        const bool currentSelectionActive = state->selection.isActive();
        const int currentSelectionStart = state->selection.getStartInt();
        const int currentSelectionEnd = state->selection.getEndInt();

        if (currentSelectionActive != lastSelectionActive)
        {
            lastSelectionActive = currentSelectionActive;
            lastSelectionStart = currentSelectionStart;
            lastSelectionEnd = currentSelectionEnd;

            if (currentSelectionActive)
            {
                startField->setValue(std::to_string(state->selection.getStartInt()));
                endField->setValue(std::to_string(state->selection.getEndInt()));
                lengthField->setValue(std::to_string(state->selection.getLengthInt()));
            }
            else
            {
                startField->setValue("");
                endField->setValue("");
                lengthField->setValue("");
            }
        }
        else if (currentSelectionActive)
        {
            if (currentSelectionStart != lastSelectionStart)
            {
                lastSelectionStart = currentSelectionStart;
                startField->setValue(std::to_string(state->selection.getStartInt()));
                lengthField->setValue(std::to_string(state->selection.getLengthInt()));
            }

            if (currentSelectionEnd != lastSelectionEnd)
            {
                lastSelectionEnd = currentSelectionEnd;
                endField->setValue(std::to_string(state->selection.getEndInt()));
                lengthField->setValue(std::to_string(state->selection.getLengthInt()));
            }
        }
    }
};
