#include "Record.hpp"

#include "../State.hpp"
#include "../gui/OptionsWindow.hpp"
#include "../gui/VuMeterAccess.hpp"
#include "Monitor.hpp"
#include "Play.hpp"
#include "DocumentDialogs.hpp"

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

    auto &session = state->getActiveDocumentSession();

    if (session.document.getSampleRate() <= 0 ||
        session.document.getChannelCount() <= 0)
    {
        state->pendingRecordAfterNewFile = true;
        showNewFileDialog(state);
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

    if (!ensureAudioInputAccess(state))
    {
        return;
    }

    const auto streamResult =
        state->audioDevices->prepareForRecording(session.document);
    if (!streamResult)
    {
        if (streamResult.failure)
        {
            reportAudioStreamFailure(state, *streamResult.failure,
                                     "Audio input unavailable");
        }
        else
        {
            reportAudioInputUnavailable(state);
        }
        return;
    }
    gui::dismissOptionsWindow(state);

    cupuacu::audio::Record recordMessage;
    recordMessage.document = &session.document;
    if (session.selection.isActive())
    {
        recordMessage.startPos =
            std::max<int64_t>(0, session.selection.getStartInt());
        recordMessage.endPos = std::max<uint64_t>(
            recordMessage.startPos, session.selection.getEndExclusiveInt());
        recordMessage.boundedToEnd = true;
    }
    else
    {
        recordMessage.startPos = std::max<int64_t>(0, session.cursor);
        recordMessage.endPos = 0;
        recordMessage.boundedToEnd = false;
    }
    recordMessage.vuMeter = gui::getVuMeterIfPresent(state);
    state->audioDevices->enqueue(std::move(recordMessage));
}
