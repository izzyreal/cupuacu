#pragma once

#include "../Constants.hpp"
#include "../State.hpp"
#include "ZoomPlanning.hpp"
#include "../gui/Waveform.hpp"
#include "../gui/WaveformRefresh.hpp"

namespace cupuacu::actions
{

    static double getMinSamplesPerPixel(const cupuacu::State *state)
    {
        const auto waveformWidth = gui::Waveform::getWaveformWidth(state);
        return planMinSamplesPerPixel(waveformWidth);
    }

    static void resetZoom(cupuacu::State *state)
    {
        auto &viewState = state->getActiveViewState();
        const auto plan = planResetZoom(
            state->getActiveDocumentSession().document.getFrameCount(),
            gui::Waveform::getWaveformWidth(state));
        viewState.samplesPerPixel = plan.samplesPerPixel;
        viewState.verticalZoom = plan.verticalZoom;
        gui::resetWaveformInteractionState(state);
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
        auto &viewState = state->getActiveViewState();
        const auto plan = planZoomInHorizontally(
            viewState.samplesPerPixel, viewState.sampleOffset,
            gui::Waveform::getWaveformWidth(state),
            state->getActiveDocumentSession().document.getFrameCount());
        if (!plan.changed)
        {
            return false;
        }

        viewState.samplesPerPixel = plan.samplesPerPixel;
        updateSampleOffset(state, plan.sampleOffset);
        gui::resetWaveformInteractionState(state);
        return true;
    }

    static bool tryZoomOutHorizontally(cupuacu::State *state)
    {
        auto &viewState = state->getActiveViewState();
        const auto plan = planZoomOutHorizontally(
            viewState.samplesPerPixel, viewState.sampleOffset,
            gui::Waveform::getWaveformWidth(state),
            state->getActiveDocumentSession().document.getFrameCount());
        if (!plan.changed)
        {
            return false;
        }

        viewState.samplesPerPixel = plan.samplesPerPixel;
        updateSampleOffset(state, plan.sampleOffset);
        gui::resetWaveformInteractionState(state);
        return true;
    }

    static void zoomInVertically(cupuacu::State *state,
                                 const uint8_t multiplier)
    {
        auto &viewState = state->getActiveViewState();
        viewState.verticalZoom += 0.3 * multiplier;
    }

    static bool tryZoomOutVertically(cupuacu::State *state,
                                     const uint8_t multiplier)
    {
        auto &viewState = state->getActiveViewState();
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
        auto &viewState = state->getActiveViewState();
        const auto plan = planZoomSelection(
            state->getActiveDocumentSession().selection.isActive(),
            state->getActiveDocumentSession().selection.getLengthInt(),
            state->getActiveDocumentSession().selection.getStartInt(),
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
