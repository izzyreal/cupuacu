#pragma once

#include "audio/AudioDevices.hpp"

#include "gui/Component.hpp"
#include "gui/LabeledField.hpp"
#include "gui/Colors.hpp"
#include "gui/MainViewAccess.hpp"
#include "gui/UiScale.hpp"
#include "gui/Waveform.hpp"

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
#include <system_error>

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
        int64_t lastSelectionLength = std::numeric_limits<int64_t>::max();

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
                state->getActiveViewState()
                    .sampleValueUnderMouseCursor;
            if (!hovered.has_value())
            {
                return "";
            }

            const auto format =
                state->getActiveDocumentSession().document.getSampleFormat();
            const auto buffer = state->getActiveDocumentSession().document.getAudioBuffer();
            const bool preserveLoadedCode =
                buffer && file::isIntegerPcmSampleFormat(format) &&
                !buffer->isDirty(hovered->channel, hovered->frame);
            const auto quantized = file::quantizedStatusSampleValue(
                format, hovered->value, preserveLoadedCode);
            if (quantized.has_value())
            {
                return formatIntegerWithThousandsSeparators(*quantized);
            }

            std::ostringstream value;
            value << std::setprecision(6) << hovered->value;
            return value.str();
        }

        static std::string
        formatIntegerWithThousandsSeparators(const int64_t value)
        {
            std::string digits = std::to_string(value);
            const bool negative = !digits.empty() && digits.front() == '-';
            const size_t start = negative ? 1 : 0;

            for (size_t i = digits.size(); i > start + 3; i -= 3)
            {
                digits.insert(i - 3, ",");
            }

            return digits;
        }

        static std::optional<int64_t> parseFrameIndex(const std::string &text)
        {
            if (text.empty())
            {
                return std::nullopt;
            }

            try
            {
                size_t consumed = 0;
                const auto value = std::stoll(text, &consumed);
                if (consumed != text.size())
                {
                    return std::nullopt;
                }
                return value;
            }
            catch (const std::exception &)
            {
                return std::nullopt;
            }
        }

        void invalidateTrackedPositionState()
        {
            lastDrawnPos = std::numeric_limits<int64_t>::max();
            lastSelectionStart = std::numeric_limits<int64_t>::max();
            lastSelectionEnd = std::numeric_limits<int64_t>::max();
            lastSelectionLength = std::numeric_limits<int64_t>::max();
            lastSelectionActive =
                !state->getActiveDocumentSession().selection.isActive();
        }

        void refreshPositionDependentUi()
        {
            invalidateTrackedPositionState();
            Waveform::setAllWaveformsDirty(state);
            requestMainViewRefresh(state);
            timerCallback();
        }

        int64_t getDocumentFrameCount() const
        {
            return state->getActiveDocumentSession().document.getFrameCount();
        }

        void applyLengthFromStart(const int64_t startFrame,
                                  const int64_t desiredLength)
        {
            auto &session = state->getActiveDocumentSession();
            const int64_t frameCount = getDocumentFrameCount();
            const int64_t clampedStart =
                std::clamp(startFrame, int64_t{0}, frameCount);
            const int64_t clampedLength = std::max<int64_t>(0, desiredLength);
            const int64_t endExclusive =
                std::clamp(clampedStart + clampedLength, clampedStart, frameCount);

            if (endExclusive == clampedStart)
            {
                session.selection.reset();
            }
            else
            {
                session.selection.setValue1(clampedStart);
                session.selection.setValue2(endExclusive);
            }
        }

        bool applyPositionEdit(const std::string &text)
        {
            const auto value = parseFrameIndex(text);
            if (!value.has_value())
            {
                return false;
            }

            updateCursorPos(state, *value);
            refreshPositionDependentUi();
            return true;
        }

        bool applyStartEdit(const std::string &text)
        {
            const auto value = parseFrameIndex(text);
            if (!value.has_value())
            {
                return false;
            }

            auto &session = state->getActiveDocumentSession();
            if (!session.selection.isActive())
            {
                updateCursorPos(state, *value);
                refreshPositionDependentUi();
                return true;
            }

            const int64_t selectionLength = session.selection.getLengthInt();
            const int64_t frameCount = getDocumentFrameCount();
            const int64_t startFrame =
                std::clamp(*value, int64_t{0},
                           std::max<int64_t>(0, frameCount - selectionLength));
            applyLengthFromStart(startFrame, selectionLength);
            refreshPositionDependentUi();
            return true;
        }

        bool applyEndEdit(const std::string &text)
        {
            const auto value = parseFrameIndex(text);
            if (!value.has_value())
            {
                return false;
            }

            auto &session = state->getActiveDocumentSession();
            const int64_t frameCount = getDocumentFrameCount();

            if (!session.selection.isActive())
            {
                const int64_t cursor = std::clamp(
                    session.cursor, int64_t{0}, std::max<int64_t>(0, frameCount));
                const int64_t inclusiveEnd = std::clamp(
                    *value, int64_t{0}, std::max<int64_t>(0, frameCount - 1));
                const int64_t selectionStart = std::min(cursor, inclusiveEnd);
                const int64_t selectionEndExclusive =
                    std::max(cursor, inclusiveEnd) + 1;
                applyLengthFromStart(
                    selectionStart, selectionEndExclusive - selectionStart);
                refreshPositionDependentUi();
                return true;
            }

            const int64_t startFrame = session.selection.getStartInt();
            const int64_t inclusiveEnd = std::clamp(
                *value, startFrame - 1, std::max<int64_t>(-1, frameCount - 1));
            applyLengthFromStart(startFrame, inclusiveEnd - startFrame + 1);
            refreshPositionDependentUi();
            return true;
        }

        bool applyLengthEdit(const std::string &text)
        {
            const auto value = parseFrameIndex(text);
            if (!value.has_value())
            {
                return false;
            }

            auto &session = state->getActiveDocumentSession();
            const int64_t startFrame =
                session.selection.isActive() ? session.selection.getStartInt()
                                             : session.cursor;
            applyLengthFromStart(startFrame, *value);
            refreshPositionDependentUi();
            return true;
        }

        void configureEditableFields()
        {
            for (auto *field : {posField, startField, endField, lengthField})
            {
                field->setEditable(true);
                field->setAllowedCharacters("0123456789");
            }

            posField->setOnSubmit(
                [this](const std::string &text)
                { return applyPositionEdit(text); });
            startField->setOnSubmit(
                [this](const std::string &text)
                { return applyStartEdit(text); });
            endField->setOnSubmit(
                [this](const std::string &text)
                { return applyEndEdit(text); });
            lengthField->setOnSubmit(
                [this](const std::string &text)
                { return applyLengthEdit(text); });
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
            configureEditableFields();
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
            const auto &session = state->getActiveDocumentSession();
            const auto &viewState =
                state->getActiveViewState();
            const int64_t currentPos =
                getPlaybackPositionIfPlaying().value_or(session.cursor);

            if (currentPos != lastDrawnPos)
            {
                if (!posField->isEditing())
                {
                    lastDrawnPos = currentPos;
                    posField->setValue(std::to_string(currentPos));
                }

                if (!session.selection.isActive() && !startField->isEditing())
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

                if (currentSelectionActive)
                {
                    if (!startField->isEditing())
                    {
                        lastSelectionStart = currentSelectionStart;
                        startField->setValue(
                            std::to_string(currentSelectionStart));
                    }
                    if (!endField->isEditing())
                    {
                        lastSelectionEnd = currentSelectionEnd;
                        endField->setValue(std::to_string(currentSelectionEnd));
                    }
                    if (!lengthField->isEditing())
                    {
                        lastSelectionLength = currentSelectionLength;
                        lengthField->setValue(
                            std::to_string(currentSelectionLength));
                    }
                }
                else
                {
                    if (!startField->isEditing())
                    {
                        lastSelectionStart = currentSelectionStart;
                        startField->setValue(std::to_string(session.cursor));
                    }
                    if (!endField->isEditing())
                    {
                        lastSelectionEnd = currentSelectionEnd;
                        endField->setValue("");
                    }
                    if (!lengthField->isEditing())
                    {
                        lastSelectionLength = currentSelectionLength;
                        lengthField->setValue(std::to_string(0));
                    }
                }
            }
            else if (currentSelectionActive)
            {
                if (currentSelectionStart != lastSelectionStart)
                {
                    if (!startField->isEditing())
                    {
                        lastSelectionStart = currentSelectionStart;
                        startField->setValue(
                            std::to_string(currentSelectionStart));
                    }
                }

                if (currentSelectionEnd != lastSelectionEnd)
                {
                    if (!endField->isEditing())
                    {
                        lastSelectionEnd = currentSelectionEnd;
                        endField->setValue(std::to_string(currentSelectionEnd));
                    }
                }

                if (currentSelectionLength != lastSelectionLength &&
                    !lengthField->isEditing())
                {
                    lastSelectionLength = currentSelectionLength;
                    lengthField->setValue(
                        std::to_string(currentSelectionLength));
                }
            }
        }
    };
} // namespace cupuacu::gui
