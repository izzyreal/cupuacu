#include "SamplePoint.hpp"

#include "../actions/audio/SetSampleValue.hpp"
#include "MainViewAccess.hpp"
#include "SamplePointInteractionPlanning.hpp"
#include "Waveform.hpp"

using namespace cupuacu::gui;
using namespace cupuacu::actions::audio;

SamplePoint::SamplePoint(State *state, const uint8_t channelIndexToUse,
                         const int64_t sampleIndexToUse)
    : ControlPointHandle(state, "Sample point idx " +
                                    std::to_string(sampleIndexToUse)),
      sampleIndex(sampleIndexToUse), channelIndex(channelIndexToUse)
{
}

uint64_t SamplePoint::getSampleIndex() const
{
    return sampleIndex;
}

float SamplePoint::getSampleValue() const
{
    return state->getActiveDocumentSession().document.getSample(channelIndex,
                                                                sampleIndex);
}

bool SamplePoint::mouseDown(const MouseEvent &e)
{
    if (!e.buttonState.left)
    {
        return false;
    }

    isDragging = true;
    setActive(true);
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
        requestMainViewRefresh(state);
        state->waveforms[channelIndex]->updateSamplePoints();
    };

    state->addUndoable(undoable);
    auto &session = state->getActiveDocumentSession();
    auto &waveformCache = session.getWaveformCache(channelIndex);
    waveformCache.invalidateSample(sampleIndex);
    waveformCache.rebuildDirty(session.document.getAudioBuffer()
                                   ->getImmutableChannelData(channelIndex)
                                   .data());

    undoable.reset();

    isDragging = false;
    setActive(false);

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
    const auto &viewState = state->getActiveViewState();
    const auto verticalZoom = viewState.verticalZoom;

    const auto dragPlan = planSamplePointDrag(
        dragYPos, e.mouseRelY, static_cast<uint16_t>(samplePointSize),
        static_cast<uint16_t>(parentHeight), verticalZoom);
    dragYPos = dragPlan.clampedY;

    setYPos(dragYPos);
    state->getActiveDocumentSession().document.setSample(
        channelIndex, sampleIndex, dragPlan.sampleValue);
    updateSampleValueUnderMouseCursor(state, dragPlan.sampleValue, channelIndex,
                                      sampleIndex);

    return true;
}
