#pragma once

#include "Component.h"

#include "../CupuacuState.h"

class SamplePoint : public Component {

private:
    const uint64_t sampleIndex;
    bool isDragging = false;
    float prevY = 0.f;
    float dragYPos = 0.f;

    int16_t getSampleValueForYPos(const int16_t y, const uint16_t h, const double v)
    {
        return ((h/2.f - y) * 32768) / (v * (h/2.f));
    }

public:
    SamplePoint(CupuacuState *state, const uint64_t sampleIndexToUse) :
        Component(state, "Sample point idx " + std::to_string(sampleIndexToUse)), sampleIndex(sampleIndexToUse)
    {}

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

        dragYPos += mouseRelY;
        setYPos(dragYPos);
        const auto vertCenter = getYPos() + (getHeight() * 0.5f);
        const auto newSampleValue = getSampleValueForYPos(vertCenter, getParent()->getHeight(), state->verticalZoom);
        state->sampleDataL[sampleIndex] = newSampleValue;
        getParent()->setDirtyRecursive();

        return true;
    }

    void onDraw(SDL_Renderer *r) override
    {
        SDL_SetRenderDrawColor(r, 0, (isMouseOver()|| isDragging) ? 255 : 185, 0, 255);
        SDL_FRect rectToFill {0, 0, (float)getWidth(), (float)getHeight()};
        SDL_RenderFillRect(r, &rectToFill);
    }
};

