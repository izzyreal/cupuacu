#pragma once

#include "Waveform.hpp"

namespace cupuacu::gui
{
    inline void clearWaveformHighlights(cupuacu::State *state)
    {
        if (!state)
        {
            return;
        }
        for (auto *waveform : state->waveforms)
        {
            if (waveform)
            {
                waveform->clearHighlight();
            }
        }
    }

    inline void refreshWaveforms(cupuacu::State *state,
                                 const bool shouldUpdateSamplePoints,
                                 const bool shouldMarkDirty)
    {
        if (!state)
        {
            return;
        }
        for (auto *waveform : state->waveforms)
        {
            if (!waveform)
            {
                continue;
            }
            if (shouldUpdateSamplePoints)
            {
                waveform->updateSamplePoints();
            }
            if (shouldMarkDirty)
            {
                waveform->setDirty();
            }
        }
    }
} // namespace cupuacu::gui
