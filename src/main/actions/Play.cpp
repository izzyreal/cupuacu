#include "Play.hpp"

#include "../State.hpp"
#include "../gui/VuMeter.hpp"

#include "audio/AudioMessage.hpp"
#include "audio/AudioDevices.hpp"

using namespace cupuacu;
using namespace cupuacu::audio;

void performStop(cupuacu::State *state)
{
    state->audioDevices->enqueue(Stop{});

    if (state->vuMeter)
    {
        state->vuMeter->startDecay();
    }
}

void cupuacu::actions::play(cupuacu::State *state)
{
    auto &session = state->activeDocumentSession;
    auto &viewState = state->mainDocumentSessionWindow->getViewState();
    auto &doc = session.document;

    if (state->audioDevices->isRecording())
    {
        performStop(state);
    }

    uint32_t channelCount = doc.getChannelCount();
    if (channelCount == 0)
    {
        return;
    }
    if (channelCount > 2)
    {
        channelCount = 2;
    }

    uint64_t totalSamples = doc.getFrameCount();
    uint64_t start = 0;
    uint64_t end = totalSamples;

    if (session.selection.isActive())
    {
        start = session.selection.getStartInt();
        end = session.selection.getEndInt();
    }
    else
    {
        start = session.cursor;
    }

    {
        Play playMsg;
        playMsg.document = &doc;
        playMsg.startPos = start;
        playMsg.endPos = end;
        playMsg.selectedChannels = viewState.selectedChannels;
        playMsg.selectionIsActive = session.selection.isActive();
        playMsg.vuMeter = state->vuMeter;
        state->audioDevices->enqueue(std::move(playMsg));
    }
}

void cupuacu::actions::requestStop(cupuacu::State *state)
{
    performStop(state);
}
