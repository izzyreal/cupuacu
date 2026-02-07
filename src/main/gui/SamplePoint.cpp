#include "SamplePoint.hpp"

#include "../actions/audio/SetSampleValue.hpp"
#include "MainView.hpp"
#include "Waveform.hpp"

using namespace cupuacu::gui;
using namespace cupuacu::actions::audio;

SamplePoint::SamplePoint(State *state, const uint8_t channelIndexToUse,
                         const int64_t sampleIndexToUse)
    : Component(state, "Sample point idx " + std::to_string(sampleIndexToUse)),
      sampleIndex(sampleIndexToUse), channelIndex(channelIndexToUse)
{
}

float SamplePoint::getSampleValueForYPos(const int16_t y, const uint16_t h,
                                         const double v,
                                         const uint16_t samplePointSize)
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
    return state->activeDocumentSession.document.getSample(channelIndex,
                                                           sampleIndex);
}

void SamplePoint::mouseEnter()
{
    setDirty();
}

void SamplePoint::mouseLeave()
{
    setDirty();
}

bool SamplePoint::mouseDown(const MouseEvent &e)
{
    if (!e.buttonState.left)
    {
        return false;
    }

    isDragging = true;
    dragYPos = getYPos();

    undoable = std::make_shared<SetSampleValue>(state, channelIndex,
                                                sampleIndex, getSampleValue());

    return true;
}

bool SamplePoint::mouseUp(const MouseEvent &e)
{
    if (!isDragging)
    {
        return false;
    }

    undoable->setNewValue(getSampleValue());
    undoable->updateGui = [state = state, channelIndex = channelIndex]
    {
        state->mainView->setDirty();
        state->waveforms[channelIndex]->updateSamplePoints();
    };

    state->addUndoable(undoable);
    auto &waveformCache =
        state->activeDocumentSession.document.getWaveformCache(channelIndex);
    waveformCache.invalidateSample(sampleIndex);
    waveformCache.rebuildDirty(
        state->activeDocumentSession.document.getAudioBuffer()
            ->getImmutableChannelData(channelIndex)
            .data());

    undoable.reset();

    isDragging = false;
    setDirty();

    return true;
}

bool SamplePoint::mouseMove(const MouseEvent &e)
{
    if (!isDragging)
    {
        return false;
    }

    const auto samplePointSize = getHeight();
    const auto parentHeight = getParent()->getHeight();
    const auto verticalZoom = state->verticalZoom;

    // Update y-position based on mouse movement
    dragYPos += e.mouseRelY;

    // Clamp y-position to allow sample point to reach drawable area edges
    constexpr float minY = 0.0f; // Top edge of sample point can reach 0
    const float maxY =
        parentHeight - samplePointSize; // Bottom edge can reach drawableHeight
    dragYPos = std::clamp(dragYPos, minY, maxY);

    // Calculate the new sample value based on the clamped y-position
    const float vertCenter = dragYPos + samplePointSize * 0.5f;
    float newSampleValue = getSampleValueForYPos(vertCenter, parentHeight,
                                                 verticalZoom, samplePointSize);

    // Clamp sample value to [-1.0, 1.0]
    newSampleValue = std::clamp(newSampleValue, -1.0f, 1.0f);

    // Update the sample point's position and sample value
    setYPos(dragYPos);
    state->activeDocumentSession.document.setSample(channelIndex, sampleIndex,
                                                    newSampleValue);
    updateSampleValueUnderMouseCursor(state, newSampleValue);

    return true;
}

void SamplePoint::onDraw(SDL_Renderer *r)
{
    SDL_SetRenderDrawColor(r, 0, isMouseOver() || isDragging ? 255 : 185, 0,
                           255);
    const SDL_FRect rectToFill{0, 0, (float)getWidth(), (float)getHeight()};
    SDL_RenderFillRect(r, &rectToFill);
}
