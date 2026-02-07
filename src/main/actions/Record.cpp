#include "Record.hpp"

#include "../State.hpp"
#include "Play.hpp"

#include "audio/AudioDevices.hpp"
#include "audio/AudioMessage.hpp"

#include <algorithm>
#include <utility>

void cupuacu::actions::record(cupuacu::State *state)
{
    if (!state || !state->audioDevices)
    {
        return;
    }

    auto &session = state->activeDocumentSession;

    if (state->audioDevices->isRecording())
    {
        return;
    }

    if (state->audioDevices->isPlaying())
    {
        requestStop(state);
    }

    cupuacu::audio::Record recordMessage;
    recordMessage.document = &session.document;
    if (session.selection.isActive())
    {
        recordMessage.startPos =
            std::max<int64_t>(0, session.selection.getStartInt());
        recordMessage.endPos = std::max<uint64_t>(
            recordMessage.startPos, session.selection.getEndInt() + 1);
        recordMessage.boundedToEnd = true;
    }
    else
    {
        recordMessage.startPos = std::max<int64_t>(0, session.cursor);
        recordMessage.endPos = 0;
        recordMessage.boundedToEnd = false;
    }
    recordMessage.vuMeter = state->vuMeter;
    state->audioDevices->enqueue(std::move(recordMessage));
}
