#pragma once

#include "Component.h"
#include "../CupuacuState.h"

class SamplePoint : public Component {

private:
    const uint64_t sampleIndex;
    const int channelIndex;
    bool isDragging = false;
    float prevY = 0.f;
    float dragYPos = 0.f;

    float getSampleValueForYPos(const int16_t y, const uint16_t h, const double v, const int samplePointSize)
    {
        const float drawableHeight = h - samplePointSize;
        return (h/2.f - y) / (v * (drawableHeight/2.f));
    }

public:
    SamplePoint(CupuacuState *state, const int channelIndexToUse, const uint64_t sampleIndexToUse) :
        Component(state, "Sample point idx " + std::to_string(sampleIndexToUse)), sampleIndex(sampleIndexToUse), channelIndex(channelIndexToUse)
    {
    }

    uint64_t getSampleIndex() const
    {
        return sampleIndex;
    }

    float getSampleValue() const
    {
        return state->document.channels[channelIndex][sampleIndex];
    }

    void mouseEnter() override
    {
        setDirty();
    }

    void mouseLeave() override
    {
        setDirty();
    }

    bool mouseLeftButtonDown(const uint8_t numClicks, const int32_t mouseX, const int32_t mouseY) override
    {
        isDragging = true;
        prevY = 0.f;
        dragYPos = getYPos();
        return true;
    }

    bool mouseLeftButtonUp(const uint8_t numClicks, const int32_t mouseX, const int32_t mouseY) override
    {
        if (!isDragging)
        {
            return false;
        }

        isDragging = false;
        setDirty();

        return true;
    }

    bool mouseMove(const int32_t mouseX,
                   const int32_t mouseY,
                   const float mouseRelY,
                   const bool leftButtonIsDown) override
    {
        if (!isDragging)
        {
            return false;
        }

        const auto samplePointSize = getHeight();
        const auto parentHeight = getParent()->getHeight();
        const auto drawableHeight = parentHeight - samplePointSize;
        const auto verticalZoom = state->verticalZoom;

        dragYPos += mouseRelY;

        // Calculate the new sample value
        const auto vertCenter = dragYPos + (getHeight() * 0.5f);
        auto newSampleValue = getSampleValueForYPos(vertCenter, parentHeight, verticalZoom, samplePointSize);

        // Clamp sample value to [-1.0, 1.0]
        newSampleValue = std::clamp(newSampleValue, -1.0f, 1.0f);

        // Recalculate y-position based on clamped sample value
        const float newYPos = (parentHeight * 0.5f) - (newSampleValue * verticalZoom * drawableHeight * 0.5f) - (getHeight() * 0.5f);

        // Clamp y-position to allow center to reach drawable area edges
        const float minY = 0.0f; // Top edge can reach 0
        const float maxY = parentHeight - samplePointSize; // Bottom edge can reach drawableHeight
        dragYPos = std::clamp(newYPos, minY, maxY);

        setYPos(dragYPos);
        state->document.channels[channelIndex][sampleIndex] = newSampleValue;
        getParent()->setDirtyRecursive();

        return true;
    }

    void onDraw(SDL_Renderer *r) override
    {
        SDL_SetRenderDrawColor(r, 0, (isMouseOver() || isDragging) ? 255 : 185, 0, 255);
        SDL_FRect rectToFill {0, 0, (float)getWidth(), (float)getHeight()};
        SDL_RenderFillRect(r, &rectToFill);
    }
};
