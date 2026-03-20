#pragma once

#include "../State.hpp"
#include "../gui/MainViewAccess.hpp"
#include "../gui/Waveform.hpp"
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

        auto &session = state->activeDocumentSession;
        auto &viewState = state->mainDocumentSessionWindow->getViewState();
        const auto frameCount = std::max<int64_t>(0, session.document.getFrameCount());
        const auto waveformWidth =
            static_cast<double>(gui::Waveform::getWaveformWidth(state));
        const bool hasInvalidHorizontalZoom = viewState.samplesPerPixel <= 0.0;

        const bool shouldResetZoomToFillWidth =
            hasInvalidHorizontalZoom ||
            (frameCount > 0 && waveformWidth > 0.0 &&
            std::ceil(waveformWidth * viewState.samplesPerPixel) >
                static_cast<double>(frameCount));

        if (shouldResetZoomToFillWidth)
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
