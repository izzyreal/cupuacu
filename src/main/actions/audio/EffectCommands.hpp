#pragma once

#include "AmplifyFade.hpp"
#include "Dynamics.hpp"
#include "EffectUtils.hpp"

#include "State.hpp"

#include <algorithm>
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

    inline double computeNormalizePercent(cupuacu::State *state)
    {
        const float peak = computeEffectPeakAbsolute(state);
        if (!(peak > 0.0f))
        {
            return 100.0;
        }
        return std::clamp(100.0 / static_cast<double>(peak), 0.0, 1000.0);
    }

    inline void performDynamics(cupuacu::State *state,
                                const double thresholdPercent,
                                const int ratioIndex)
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

        const auto undoable = std::make_shared<cupuacu::actions::audio::Dynamics>(
            state, thresholdPercent, ratioIndex);
        state->addAndDoUndoable(undoable);
    }
} // namespace cupuacu::actions::audio
