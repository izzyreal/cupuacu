#include "Play.h"

#include "../State.h"
#include "../gui/VuMeter.h"

#include "AudioDevice.hpp"
#include "AudioMessage.hpp"
#include "AudioDevices.hpp"

using namespace cupuacu;

void performStop(cupuacu::State *state)
{
    state->audioDevices->getOutputDevice()->enqueue(Stop{});

    if (state->vuMeter)
    {
        state->vuMeter->startDecay();
    }
}

void cupuacu::actions::play(cupuacu::State *state)
{
    uint32_t channelCount = state->document.getChannelCount();
    if (channelCount == 0)
    {
        return;
    }
    if (channelCount > 2)
    {
        channelCount = 2;
    }

    uint64_t totalSamples = state->document.getFrameCount();
    uint64_t start = 0;
    uint64_t end = totalSamples;

    if (state->selection.isActive())
    {
        start = state->selection.getStartInt();
        end = state->selection.getEndInt();
    }
    else
    {
        start = state->cursor;
    }

    {
        auto device = state->audioDevices->getOutputDevice();
        Play playMsg;
        playMsg.document = &state->document;
        playMsg.startPos = start;
        playMsg.endPos = end;
        playMsg.selectedChannels = state->selectedChannels;
        playMsg.selectionIsActive = state->selection.isActive();
        playMsg.vuMeter = state->vuMeter;
        device->enqueue(std::move(playMsg));
    }
}

void cupuacu::actions::requestStop(cupuacu::State *state)
{
    performStop(state);
}
