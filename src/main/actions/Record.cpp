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

    if (state->audioDevices->isRecording())
    {
        return;
    }

    if (state->audioDevices->isPlaying())
    {
        requestStop(state);
    }

    cupuacu::audio::Record recordMessage;
    recordMessage.document = &state->document;
    recordMessage.startPos = std::max<int64_t>(0, state->cursor);
    recordMessage.vuMeter = state->vuMeter;
    state->audioDevices->enqueue(std::move(recordMessage));
}
