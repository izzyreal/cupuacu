#pragma once

#include "../State.hpp"
#include "../gui/MainViewAccess.hpp"
#include "../gui/Waveform.hpp"
#include "ViewPolicyPlanning.hpp"
#include "Zoom.hpp"

#include <algorithm>
#include <cmath>

namespace cupuacu::actions
{
    inline void applyDurationChangeViewPolicy(cupuacu::State *state)
    {
        if (!state || !state->mainDocumentSessionWindow)
        {
            return;
        }

        auto &session = state->getActiveDocumentSession();
        auto &viewState = state->getActiveViewState();
        const auto frameCount = std::max<int64_t>(0, session.document.getFrameCount());
        const auto waveformWidth =
            static_cast<double>(gui::Waveform::getWaveformWidth(state));
        const auto plan = planDurationChangeViewPolicy(
            frameCount, waveformWidth, viewState.samplesPerPixel);

        if (plan.shouldResetZoomToFillWidth)
        {
            resetZoom(state);
        }
        else
        {
            updateSampleOffset(state, viewState.sampleOffset);
        }

        gui::Waveform::updateAllSamplePoints(state);
        gui::Waveform::setAllWaveformsDirty(state);
        gui::requestMainViewRefresh(state);
    }
} // namespace cupuacu::actions
