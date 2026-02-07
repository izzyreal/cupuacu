#pragma once

#include "../Constants.hpp"
#include "../State.hpp"
#include "../gui/Waveform.hpp"

namespace cupuacu::actions
{

    static double getMinSamplesPerPixel(const cupuacu::State *state)
    {
        const auto waveformWidth = gui::Waveform::getWaveformWidth(state);
        return 1.0 / waveformWidth;
    }

    static void resetZoom(cupuacu::State *state)
    {
        auto &viewState = state->mainDocumentSessionWindow->getViewState();
        const auto waveformWidth = gui::Waveform::getWaveformWidth(state);
        if (waveformWidth == 0)
        {
            viewState.samplesPerPixel = 0;
        }
        else
        {
            viewState.samplesPerPixel =
                state->activeDocumentSession.document.getFrameCount() /
                (float)waveformWidth;
        }

        viewState.verticalZoom = INITIAL_VERTICAL_ZOOM;

        resetSampleValueUnderMouseCursor(state);

        for (auto w : state->waveforms)
        {
            w->clearHighlight();
        }

        viewState.sampleOffset = 0;
    }

    static bool tryZoomInHorizontally(cupuacu::State *state)
    {
        auto &viewState = state->mainDocumentSessionWindow->getViewState();
        const double minSamplesPerPixel = getMinSamplesPerPixel(state);

        if (viewState.samplesPerPixel <= minSamplesPerPixel)
        {
            return false;
        }

        viewState.samplesPerPixel =
            std::max(viewState.samplesPerPixel / 2.0, minSamplesPerPixel);

        resetSampleValueUnderMouseCursor(state);

        for (auto w : state->waveforms)
        {
            w->clearHighlight();
        }

        return true;
    }

    static bool tryZoomOutHorizontally(cupuacu::State *state)
    {
        auto &viewState = state->mainDocumentSessionWindow->getViewState();
        const auto waveformWidth = gui::Waveform::getWaveformWidth(state);
        const float maxSamplesPerPixel =
            static_cast<float>(
                state->activeDocumentSession.document.getFrameCount()) /
            waveformWidth;

        if (viewState.samplesPerPixel >= maxSamplesPerPixel)
        {
            return false;
        }

        const auto centerSampleIndex =
            ((waveformWidth / 2.0 + 0.5) * viewState.samplesPerPixel) +
            viewState.sampleOffset;

        viewState.samplesPerPixel =
            std::min(viewState.samplesPerPixel * 2.0,
                     static_cast<double>(maxSamplesPerPixel));

        const auto newSampleOffset =
            centerSampleIndex -
            ((waveformWidth / 2.0 + 0.5) * viewState.samplesPerPixel);

        updateSampleOffset(state, newSampleOffset);

        resetSampleValueUnderMouseCursor(state);

        for (auto w : state->waveforms)
        {
            w->clearHighlight();
        }

        return true;
    }

    static void zoomInVertically(cupuacu::State *state,
                                 const uint8_t multiplier)
    {
        auto &viewState = state->mainDocumentSessionWindow->getViewState();
        viewState.verticalZoom += 0.3 * multiplier;
    }

    static bool tryZoomOutVertically(cupuacu::State *state,
                                     const uint8_t multiplier)
    {
        auto &viewState = state->mainDocumentSessionWindow->getViewState();
        if (viewState.verticalZoom <= 1)
        {
            return false;
        }

        viewState.verticalZoom -= 0.3 * multiplier;

        if (viewState.verticalZoom < 1)
        {
            viewState.verticalZoom = 1;
        }

        return true;
    }

    static bool tryZoomSelection(cupuacu::State *state)
    {
        auto &viewState = state->mainDocumentSessionWindow->getViewState();
        if (!state->activeDocumentSession.selection.isActive() ||
            state->activeDocumentSession.selection.getLengthInt() < 1)
        {
            return false;
        }

        viewState.verticalZoom = INITIAL_VERTICAL_ZOOM;

        const auto waveformWidth = gui::Waveform::getWaveformWidth(state);
        const auto selectionLength =
            state->activeDocumentSession.selection.getLengthInt();

        viewState.samplesPerPixel =
            selectionLength / static_cast<double>(waveformWidth);
        viewState.sampleOffset =
            state->activeDocumentSession.selection.getStartInt();
        return true;
    }
} // namespace cupuacu::actions
