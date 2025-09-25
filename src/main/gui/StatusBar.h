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

    float getSampleValueAtMousePosition()
    {
        if (const auto *samplePoint = dynamic_cast<SamplePoint*>(state->capturingComponent); samplePoint != nullptr)
        {
            return samplePoint->getSampleValue();
        }

        const auto mouseX = state->mouseX;
        const auto mouseY = state->mouseY;
        const auto samplesPerPixel = state->samplesPerPixel;
        const auto sampleOffset = state->sampleOffset;

        // Find the channel under the mouse
        int channelIndex = -1;
        for (size_t i = 0; i < state->waveforms.size(); ++i)
        {
            const auto& waveform = state->waveforms[i];
            const int yPos = waveform->getYPos();
            const int height = waveform->getHeight();
            if (mouseY >= yPos && mouseY < yPos + height)
            {
                channelIndex = static_cast<int>(i);
                break;
            }
        }

        // If no channel is under the mouse or channels are empty, return 0.0
        if (channelIndex == -1 || state->document.channels.empty() || state->document.channels[channelIndex].empty())
        {
            return 0.0f;
        }

        const auto& sampleData = state->document.channels[channelIndex];

        if (mouseX < 0 || mouseX >= getParent()->getWidth() || sampleData.empty())
        {
            return 0.0f;
        }

        const size_t sampleIndex = WaveformsOverlay::getSamplePosForMouseX(mouseX, samplesPerPixel, sampleOffset, state->document.getFrameCount());

        if (sampleIndex >= sampleData.size())
        {
            return -1;
        }

        const bool showSamplePoints = Waveform::shouldShowSamplePoints(samplesPerPixel, state->hardwarePixelsPerAppPixel);

        if (showSamplePoints)
        {
            // High zoom: return exact sample value
            return sampleData[sampleIndex];
        }
        else
        {
            // Low zoom: interpolate between samples
            const double fractionalIndex = mouseX * samplesPerPixel + sampleOffset;
            const size_t indexFloor = static_cast<size_t>(std::floor(fractionalIndex));
            const size_t indexCeil = std::min(indexFloor + 1, sampleData.size() - 1);
            const double fraction = fractionalIndex - indexFloor;

            // Linear interpolation
            return sampleData[indexFloor] + static_cast<float>(fraction) * (sampleData[indexCeil] - sampleData[indexFloor]);
        }
    }

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
        float scale = 4.0f / state->hardwarePixelsPerAppPixel;
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

        posField->setValue(std::to_string((int)state->playbackPosition.load()));

        if (state->selection.isActive())
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

        // Set the sample value under the mouse cursor
        const float sampleValue = getSampleValueAtMousePosition();
        if (sampleValue == -1)
        {
            valueField->setValue("");
        }
        else
        {
            char buffer[16];
            snprintf(buffer, sizeof(buffer), "%.5f", sampleValue);
            valueField->setValue(buffer);
        }
    }

    void timerCallback() override
    {
        setDirtyRecursive();
    }
};
