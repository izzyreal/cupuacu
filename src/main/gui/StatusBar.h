#pragma once

#include "Component.h"
#include "LabeledField.h"
#include "../CupuacuState.h"

#include <SDL3/SDL.h>
#include <limits>
#include <optional>
#include <string>

class StatusBar : public Component {
private:
    LabeledField* posField = nullptr;
    LabeledField* startField = nullptr;
    LabeledField* endField = nullptr;
    LabeledField* lengthField = nullptr;
    LabeledField* valueField = nullptr;

    int64_t lastPlaybackPosition = std::numeric_limits<int64_t>::max();
    int64_t lastSelectionStart = std::numeric_limits<int64_t>::max();
    int64_t lastSelectionEnd = std::numeric_limits<int64_t>::max();
    bool lastSelectionActive = false;
    std::optional<float> lastSampleValueUnderMouseCursor;

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
        const float scale = 4.0f / state->pixelScale;
        const int fieldWidth = int(120 * scale);
        const int fieldHeight = int(getHeight() * scale);

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
        const int64_t currentPlaybackPosition = state->playbackPosition.load();

        if (currentPlaybackPosition != lastPlaybackPosition)
        {
            lastPlaybackPosition = currentPlaybackPosition;
            posField->setValue(std::to_string(currentPlaybackPosition));
        }

        if (lastSampleValueUnderMouseCursor != state->sampleValueUnderMouseCursor)
        {
            lastSampleValueUnderMouseCursor = state->sampleValueUnderMouseCursor;

            if (state->sampleValueUnderMouseCursor.has_value())
            {
                valueField->setValue(std::to_string(*state->sampleValueUnderMouseCursor));
            }
            else
            {
                valueField->setValue("");
            }
        }

        const bool currentSelectionActive = state->selection.isActive();
        const int64_t currentSelectionStart = state->selection.getStartInt();
        const int64_t currentSelectionEnd = state->selection.getEndInt();

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
