#pragma once

#include "audio/AudioDevices.hpp"

#include "gui/Component.hpp"
#include "gui/LabeledField.hpp"
#include "gui/Colors.hpp"

#include "State.hpp"

#include <SDL3/SDL.h>

#include <limits>
#include <optional>
#include <string>

namespace cupuacu::gui
{
    class StatusBar : public Component
    {
    private:
        LabeledField *posField;
        LabeledField *startField;
        LabeledField *endField;
        LabeledField *lengthField;
        LabeledField *valueField;

        int64_t lastDrawnPos = std::numeric_limits<int64_t>::max();
        int64_t lastSelectionStart = std::numeric_limits<int64_t>::max();
        int64_t lastSelectionEnd = std::numeric_limits<int64_t>::max();

        // We assume the user starts without a selection.
        // We have to be careful after implementing state restoration and the
        // state is restored to an active selection.
        bool lastSelectionActive = true;
        std::optional<float> lastSampleValueUnderMouseCursor;

    public:
        StatusBar(State *stateToUse) : Component(stateToUse, "StatusBar")
        {
            posField =
                emplaceChild<LabeledField>(state, "Pos", Colors::background);
            startField =
                emplaceChild<LabeledField>(state, "St", Colors::background);
            endField =
                emplaceChild<LabeledField>(state, "End", Colors::background);
            lengthField =
                emplaceChild<LabeledField>(state, "Len", Colors::background);
            valueField =
                emplaceChild<LabeledField>(state, "Val", Colors::background);
        }

        void resized() override
        {
            const float scale = 4.0f / state->pixelScale;
            const int fieldWidth = int(80 * scale);
            const int fieldHeight = int(getHeight());

            posField->setBounds(0, 0, fieldWidth, fieldHeight);
            startField->setBounds(fieldWidth, 0, fieldWidth, fieldHeight);
            endField->setBounds(2 * fieldWidth, 0, fieldWidth, fieldHeight);
            lengthField->setBounds(3 * fieldWidth, 0, fieldWidth, fieldHeight);
            valueField->setBounds(4 * fieldWidth, 0, fieldWidth, fieldHeight);
        }

        void onDraw(SDL_Renderer *renderer) override
        {
            Helpers::fillRect(renderer, getLocalBounds(), Colors::background);
        }

        void timerCallback() override
        {
            const auto &session = state->activeDocumentSession;
            const auto &viewState =
                state->mainDocumentSessionWindow->getViewState();
            const bool isPlaying = state->audioDevices->isPlaying();

            const int64_t currentPos =
                isPlaying ? state->audioDevices->getPlaybackPosition()
                          : session.cursor;

            if (currentPos != lastDrawnPos)
            {
                lastDrawnPos = currentPos;
                posField->setValue(std::to_string(currentPos));

                if (!session.selection.isActive())
                {
                    startField->setValue(std::to_string(currentPos));
                }
            }

            if (lastSampleValueUnderMouseCursor !=
                viewState.sampleValueUnderMouseCursor)
            {
                lastSampleValueUnderMouseCursor =
                    viewState.sampleValueUnderMouseCursor;

                if (viewState.sampleValueUnderMouseCursor.has_value())
                {
                    valueField->setValue(
                        std::to_string(*viewState.sampleValueUnderMouseCursor));
                }
                else
                {
                    valueField->setValue("");
                }
            }

            const bool currentSelectionActive = session.selection.isActive();
            const int64_t currentSelectionStart =
                session.selection.getStartInt();
            const int64_t currentSelectionEnd = session.selection.getEndInt();

            if (currentSelectionActive != lastSelectionActive)
            {
                lastSelectionActive = currentSelectionActive;
                lastSelectionStart = currentSelectionStart;
                lastSelectionEnd = currentSelectionEnd;

                if (currentSelectionActive)
                {
                    startField->setValue(
                        std::to_string(session.selection.getStartInt()));
                    endField->setValue(
                        std::to_string(session.selection.getEndInt()));
                    lengthField->setValue(
                        std::to_string(session.selection.getLengthInt()));
                }
                else
                {
                    startField->setValue(std::to_string(session.cursor));
                    endField->setValue("");
                    lengthField->setValue(std::to_string(0));
                }
            }
            else if (currentSelectionActive)
            {
                if (currentSelectionStart != lastSelectionStart)
                {
                    lastSelectionStart = currentSelectionStart;
                    startField->setValue(
                        std::to_string(session.selection.getStartInt()));
                    lengthField->setValue(
                        std::to_string(session.selection.getLengthInt()));
                }

                if (currentSelectionEnd != lastSelectionEnd)
                {
                    lastSelectionEnd = currentSelectionEnd;
                    endField->setValue(
                        std::to_string(session.selection.getEndInt()));
                    lengthField->setValue(
                        std::to_string(session.selection.getLengthInt()));
                }
            }
        }
    };
} // namespace cupuacu::gui
