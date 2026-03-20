#pragma once

#include "audio/AudioDevices.hpp"

#include "gui/Component.hpp"
#include "gui/LabeledField.hpp"
#include "gui/Colors.hpp"
#include "gui/UiScale.hpp"

#include "State.hpp"
#include "actions/DocumentLifecycle.hpp"
#include "file/SampleQuantization.hpp"

#include <SDL3/SDL.h>

#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
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
        LabeledField *sampleRateField;
        LabeledField *bitDepthField;

        int64_t lastDrawnPos = std::numeric_limits<int64_t>::max();
        int64_t lastSelectionStart = std::numeric_limits<int64_t>::max();
        int64_t lastSelectionEnd = std::numeric_limits<int64_t>::max();

        // We assume the user starts without a selection.
        // We have to be careful after implementing state restoration and the
        // state is restored to an active selection.
        bool lastSelectionActive = true;
        std::optional<HoveredSampleInfo> lastSampleValueUnderMouseCursor;
        int lastSampleRate = std::numeric_limits<int>::min();
        std::string lastBitDepthLabel;

        std::optional<int64_t> getPlaybackPositionIfPlaying() const
        {
            if (!state || !state->audioDevices ||
                !state->audioDevices->isPlaying())
            {
                return std::nullopt;
            }

            return state->audioDevices->getPlaybackPosition();
        }

        std::string formatHoveredSampleValue() const
        {
            const auto &hovered =
                state->mainDocumentSessionWindow->getViewState()
                    .sampleValueUnderMouseCursor;
            if (!hovered.has_value())
            {
                return "";
            }

            const auto format =
                state->activeDocumentSession.document.getSampleFormat();
            const auto buffer = state->activeDocumentSession.document.getAudioBuffer();
            const bool preserveLoadedCode =
                buffer && file::isIntegerPcmSampleFormat(format) &&
                !buffer->isDirty(hovered->channel, hovered->frame);
            const auto quantized = file::quantizedStatusSampleValue(
                format, hovered->value, preserveLoadedCode);
            if (quantized.has_value())
            {
                return std::to_string(*quantized);
            }

            std::ostringstream value;
            value << std::setprecision(6) << hovered->value;
            return value.str();
        }

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
            sampleRateField =
                emplaceChild<LabeledField>(state, "Rate", Colors::background);
            bitDepthField =
                emplaceChild<LabeledField>(state, "Depth", Colors::background);
        }

        void resized() override
        {
            const int fieldWidth = getWidth() / 7;
            const int fieldHeight = int(getHeight());

            posField->setBounds(0, 0, fieldWidth, fieldHeight);
            startField->setBounds(fieldWidth, 0, fieldWidth, fieldHeight);
            endField->setBounds(2 * fieldWidth, 0, fieldWidth, fieldHeight);
            lengthField->setBounds(3 * fieldWidth, 0, fieldWidth, fieldHeight);
            valueField->setBounds(4 * fieldWidth, 0, fieldWidth, fieldHeight);
            sampleRateField->setBounds(5 * fieldWidth, 0, fieldWidth, fieldHeight);
            bitDepthField->setBounds(6 * fieldWidth, 0, getWidth() - 6 * fieldWidth,
                                     fieldHeight);
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
            const int64_t currentPos =
                getPlaybackPositionIfPlaying().value_or(session.cursor);

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
                    valueField->setValue(formatHoveredSampleValue());
                }
                else
                {
                    valueField->setValue("");
                }
            }

            const int sampleRate = session.document.getSampleRate();
            if (sampleRate != lastSampleRate)
            {
                lastSampleRate = sampleRate;
                sampleRateField->setValue(
                    sampleRate > 0 ? std::to_string(sampleRate) : "");
            }

            const std::string bitDepthLabel =
                cupuacu::actions::sampleFormatLabel(
                    session.document.getSampleFormat());
            if (bitDepthLabel != lastBitDepthLabel)
            {
                lastBitDepthLabel = bitDepthLabel;
                bitDepthField->setValue(bitDepthLabel);
            }

            const bool currentSelectionActive = session.selection.isActive();
            const int64_t currentSelectionStart =
                session.selection.getStartInt();
            const int64_t currentSelectionEnd = session.selection.getEndInt();
            const int64_t currentSelectionLength =
                session.selection.getLengthInt();

            if (currentSelectionActive != lastSelectionActive)
            {
                lastSelectionActive = currentSelectionActive;
                lastSelectionStart = currentSelectionStart;
                lastSelectionEnd = currentSelectionEnd;

                if (currentSelectionActive)
                {
                    startField->setValue(std::to_string(currentSelectionStart));
                    endField->setValue(std::to_string(currentSelectionEnd));
                    lengthField->setValue(std::to_string(currentSelectionLength));
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
                    startField->setValue(std::to_string(currentSelectionStart));
                    lengthField->setValue(
                        std::to_string(currentSelectionLength));
                }

                if (currentSelectionEnd != lastSelectionEnd)
                {
                    lastSelectionEnd = currentSelectionEnd;
                    endField->setValue(std::to_string(currentSelectionEnd));
                    lengthField->setValue(
                        std::to_string(currentSelectionLength));
                }
            }
        }
    };
} // namespace cupuacu::gui
