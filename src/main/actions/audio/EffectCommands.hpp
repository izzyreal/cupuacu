#pragma once

#include "AmplifyFade.hpp"

#include "State.hpp"

#include <memory>

namespace cupuacu::actions::audio
{
    inline void performAmplifyFade(cupuacu::State *state,
                                   const double startPercent,
                                   const double endPercent,
                                   const int curveIndex)
    {
        if (!state ||
            state->activeDocumentSession.document.getFrameCount() <= 0 ||
            state->activeDocumentSession.document.getChannelCount() <= 0)
        {
            return;
        }

        const bool hasSelection = state->activeDocumentSession.selection.isActive();
        if (hasSelection &&
            state->activeDocumentSession.selection.getLengthInt() <= 0)
        {
            return;
        }

        const auto undoable = std::make_shared<cupuacu::actions::audio::AmplifyFade>(
            state, startPercent, endPercent, curveIndex);
        state->addAndDoUndoable(undoable);
    }
} // namespace cupuacu::actions::audio
