#include "SamplePoint.h"

SamplePoint::SamplePoint(CupuacuState *state, const int channelIndexToUse, const uint64_t sampleIndexToUse) :
    Component(state, "Sample point idx " + std::to_string(sampleIndexToUse)), sampleIndex(sampleIndexToUse), channelIndex(channelIndexToUse)
{
}

float SamplePoint::getSampleValueForYPos(const int16_t y, const uint16_t h, const double v, const int samplePointSize)
{
    const float drawableHeight = h - samplePointSize;
    return (h / 2.f - y) / (v * (drawableHeight / 2.f));
}

uint64_t SamplePoint::getSampleIndex() const
{
    return sampleIndex;
}

float SamplePoint::getSampleValue() const
{
    return state->document.channels[channelIndex][sampleIndex];
}

void SamplePoint::mouseEnter()
{
    setDirty();
}

void SamplePoint::mouseLeave()
{
    setDirty();
}

bool SamplePoint::mouseLeftButtonDown(const uint8_t numClicks, const int32_t mouseX, const int32_t mouseY)
{
    isDragging = true;
    prevY = 0.f;
    dragYPos = getYPos();
    return true;
}

bool SamplePoint::mouseLeftButtonUp(const uint8_t numClicks, const int32_t mouseX, const int32_t mouseY)
{
    if (!isDragging)
    {
        return false;
    }

    isDragging = false;
    state->capturingComponent = nullptr;
    setDirty();

    return true;
}

bool SamplePoint::mouseMove(const int32_t mouseX, const int32_t mouseY, const float mouseRelY, const bool leftButtonIsDown)
{
    if (!isDragging)
    {
        return false;
    }

    const auto samplePointSize = getHeight();
    const auto parentHeight = getParent()->getHeight();
    const auto verticalZoom = state->verticalZoom;

    // Update y-position based on mouse movement
    dragYPos += mouseRelY;

    // Clamp y-position to allow sample point to reach drawable area edges
    const float minY = 0.0f; // Top edge of sample point can reach 0
    const float maxY = parentHeight - samplePointSize; // Bottom edge can reach drawableHeight
    dragYPos = std::clamp(dragYPos, minY, maxY);

    // Calculate the new sample value based on the clamped y-position
    const float vertCenter = dragYPos + (samplePointSize * 0.5f);
    float newSampleValue = getSampleValueForYPos(vertCenter, parentHeight, verticalZoom, samplePointSize);

    // Clamp sample value to [-1.0, 1.0]
    newSampleValue = std::clamp(newSampleValue, -1.0f, 1.0f);

    // Update the sample point's position and sample value
    setYPos(dragYPos);
    state->document.channels[channelIndex][sampleIndex] = newSampleValue;
    getParent()->setDirtyRecursive();

    return true;
}

void SamplePoint::onDraw(SDL_Renderer *r)
{
    SDL_SetRenderDrawColor(r, 0, (isMouseOver() || isDragging) ? 255 : 185, 0, 255);
    SDL_FRect rectToFill {0, 0, (float)getWidth(), (float)getHeight()};
    SDL_RenderFillRect(r, &rectToFill);
}
