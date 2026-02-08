#include "Play.hpp"

#include "../State.hpp"
#include "../gui/VuMeter.hpp"
#include "playback/PlaybackRange.hpp"

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

    const auto range =
        cupuacu::playback::computeRangeForPlay(session,
                                               state->loopPlaybackEnabled);
    const uint64_t start = range.start;
    const uint64_t end = range.end;

    {
        Play playMsg;
        playMsg.document = &doc;
        playMsg.startPos = start;
        playMsg.endPos = end;
        playMsg.loopEnabled = state->loopPlaybackEnabled;
        playMsg.selectedChannels = viewState.selectedChannels;
        playMsg.selectionIsActive = session.selection.isActive();
        playMsg.vuMeter = state->vuMeter;
        state->audioDevices->enqueue(std::move(playMsg));
    }

    state->playbackRangeStart = start;
    state->playbackRangeEnd = end;
}

void cupuacu::actions::requestStop(cupuacu::State *state)
{
    performStop(state);
}
