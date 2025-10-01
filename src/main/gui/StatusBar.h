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
    LabeledField* posField;
    LabeledField* startField;
    LabeledField* endField;
    LabeledField* lengthField;
    LabeledField* valueField;

    int64_t lastDrawnPos = std::numeric_limits<int64_t>::max();
    int64_t lastSelectionStart = std::numeric_limits<int64_t>::max();
    int64_t lastSelectionEnd = std::numeric_limits<int64_t>::max();

    // We assume the user starts without a selection.
    // We have to be careful after implementing state restoration and the state
    // is restored to an active selection.
    bool lastSelectionActive = true;
    std::optional<float> lastSampleValueUnderMouseCursor;

public:
    StatusBar(CupuacuState* stateToUse)
        : Component(stateToUse, "StatusBar")
    {
        posField = emplaceChild<LabeledField>(state, "Pos", Colors::background);
        startField = emplaceChild<LabeledField>(state, "St", Colors::background);
        endField = emplaceChild<LabeledField>(state, "End", Colors::background);
        lengthField = emplaceChild<LabeledField>(state, "Len", Colors::background);
        valueField = emplaceChild<LabeledField>(state, "Val", Colors::background);
    }

    void resized() override
    {
        const float scale = 4.0f / state->pixelScale;
        const int fieldWidth = int(120 * scale);
        const int fieldHeight = int(getHeight());

        posField->setBounds(0, 0, fieldWidth, fieldHeight);
        startField->setBounds(fieldWidth, 0, fieldWidth, fieldHeight);
        endField->setBounds(2 * fieldWidth, 0, fieldWidth, fieldHeight);
        lengthField->setBounds(3 * fieldWidth, 0, fieldWidth, fieldHeight);
        valueField->setBounds(4 * fieldWidth, 0, fieldWidth, fieldHeight);
    }

    void onDraw(SDL_Renderer* renderer) override
    {
        Helpers::fillRect(renderer, getLocalBounds(), Colors::background);
    }

    void timerCallback() override
    {
        const bool isPlaying = state->isPlaying.load();

        const int64_t currentPos = isPlaying ? state->playbackPosition.load() : state->cursor;

        if (currentPos != lastDrawnPos)
        {
            lastDrawnPos = currentPos;
            posField->setValue(std::to_string(currentPos));

            if (!state->selection.isActive())
            {
                startField->setValue(std::to_string(currentPos));
            }
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
                startField->setValue(std::to_string(state->cursor));
                endField->setValue("");
                lengthField->setValue(std::to_string(0));
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
