#pragma once

#include "../Constants.hpp"
#include "../State.hpp"
#include "ZoomPlanning.hpp"
#include "../gui/Waveform.hpp"

namespace cupuacu::actions
{

    static double getMinSamplesPerPixel(const cupuacu::State *state)
    {
        const auto waveformWidth = gui::Waveform::getWaveformWidth(state);
        return planMinSamplesPerPixel(waveformWidth);
    }

    static void resetZoom(cupuacu::State *state)
    {
        auto &viewState = state->mainDocumentSessionWindow->getViewState();
        const auto plan = planResetZoom(
            state->activeDocumentSession.document.getFrameCount(),
            gui::Waveform::getWaveformWidth(state));
        viewState.samplesPerPixel = plan.samplesPerPixel;
        viewState.verticalZoom = plan.verticalZoom;

        resetSampleValueUnderMouseCursor(state);

        for (auto w : state->waveforms)
        {
            w->clearHighlight();
        }

        updateSampleOffset(state, plan.sampleOffset);
    }

    static void resetZoomAndRefreshWaveforms(cupuacu::State *state)
    {
        resetZoom(state);
        gui::Waveform::updateAllSamplePoints(state);
        gui::Waveform::setAllWaveformsDirty(state);
    }

    static bool tryZoomInHorizontally(cupuacu::State *state)
    {
        auto &viewState = state->mainDocumentSessionWindow->getViewState();
        const auto plan = planZoomInHorizontally(
            viewState.samplesPerPixel, viewState.sampleOffset,
            gui::Waveform::getWaveformWidth(state),
            state->activeDocumentSession.document.getFrameCount());
        if (!plan.changed)
        {
            return false;
        }

        viewState.samplesPerPixel = plan.samplesPerPixel;
        updateSampleOffset(state, plan.sampleOffset);

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
        const auto plan = planZoomOutHorizontally(
            viewState.samplesPerPixel, viewState.sampleOffset,
            gui::Waveform::getWaveformWidth(state),
            state->activeDocumentSession.document.getFrameCount());
        if (!plan.changed)
        {
            return false;
        }

        viewState.samplesPerPixel = plan.samplesPerPixel;
        updateSampleOffset(state, plan.sampleOffset);

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
        const auto plan = planZoomSelection(
            state->activeDocumentSession.selection.isActive(),
            state->activeDocumentSession.selection.getLengthInt(),
            state->activeDocumentSession.selection.getStartInt(),
            gui::Waveform::getWaveformWidth(state));
        if (!plan.changed)
        {
            return false;
        }

        viewState.verticalZoom = plan.verticalZoom;
        viewState.samplesPerPixel = plan.samplesPerPixel;
        updateSampleOffset(state, plan.sampleOffset);
        return true;
    }
} // namespace cupuacu::actions
