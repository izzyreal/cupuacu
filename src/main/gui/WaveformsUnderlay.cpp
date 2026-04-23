#include "WaveformsUnderlay.hpp"

#include "Waveform.hpp"
#include "WaveformsUnderlayPlanning.hpp"
#include "WaveformRefresh.hpp"
#include "SnapPlanning.hpp"
#include "Window.hpp"
#include <algorithm>
#include <cmath>

#include <cassert>

using namespace cupuacu::gui;

namespace
{
    constexpr double kPixelsPerWheelUnit = 15;
    constexpr double kWheelSmoothingFactor = 0.3;
    constexpr double kWheelSnapThresholdPixels = 0.5;
    constexpr uint64_t kWheelStreamTimeoutMs = 20;
} // namespace

WaveformsUnderlay::WaveformsUnderlay(State *stateToUse)
    : Component(stateToUse, "WaveformsUnderlay")
{
}

void WaveformsUnderlay::mouseLeave()
{
    resetSampleValueUnderMouseCursor(state);
}

bool WaveformsUnderlay::mouseDown(const MouseEvent &e)
{
    auto &session = state->getActiveDocumentSession();
    auto &doc = session.document;
    auto &viewState = state->getActiveViewState();

    lastNumClicks = e.numClicks;
    viewState.selectedMarkerId.reset();

    handleChannelSelection(e.mouseYi, true);

    const auto samplesPerPixel = viewState.samplesPerPixel;
    if (doc.getFrameCount() <= 0 || samplesPerPixel <= 0.0)
    {
        session.selection.reset();
        session.cursor = 0;
        Waveform::setAllWaveformsDirty(state);
        return false;
    }

    if (e.numClicks >= 2)
    {
        draggedSelectionEdge.reset();
        int64_t startSample = 0;
        int64_t endSample = 0;
        if (!planSnappedVisibleRangeSelection(state, e.mouseXf, startSample,
                                              endSample))
        {
            return false;
        }

        session.selection.setValue1(startSample);
        session.selection.setValue2(endSample);

        Waveform::setAllWaveformsDirty(state);

        return true;
    }

    const auto *keyboard = SDL_GetKeyboardState(NULL);
    const bool shiftPressed =
        keyboard[SDL_SCANCODE_LSHIFT] || keyboard[SDL_SCANCODE_RSHIFT];

    const int64_t samplePos =
        planSnappedMouseSamplePosition(state, e.mouseXf, true);

    if (shiftPressed)
    {
        const double selectionCenter =
            (session.selection.getStart() + session.selection.getEnd()) * 0.5;
        const double start = session.selection.getStart();
        const double end = session.selection.getEnd();

        if (samplePos < selectionCenter)
        {
            draggedSelectionEdge = SnapSelectionEdge::Start;
            session.selection.setValue1(end);
            session.selection.setValue2(samplePos);
        }
        else
        {
            draggedSelectionEdge = SnapSelectionEdge::End;
            session.selection.setValue1(start);
            session.selection.setValue2(samplePos);
        }
    }
    else
    {
        draggedSelectionEdge = SnapSelectionEdge::End;
        session.selection.reset();
        session.selection.setValue1(samplePos);
        session.cursor = session.selection.getStartInt();
    }

    Waveform::setAllWaveformsDirty(state);

    return true;
}

void WaveformsUnderlay::handleChannelSelection(
    const int32_t mouseY, const bool isMouseDownEvent) const
{
    auto &viewState = state->getActiveViewState();
    viewState.hoveringOverChannels = planWaveformsUnderlayHoveredChannels(
        mouseY, getHeight(), static_cast<int>(state->waveforms.size()));

    if ((getWindow() && getWindow()->getCapturingComponent() == this) ||
        isMouseDownEvent)
    {
        viewState.selectedChannels = viewState.hoveringOverChannels;
    }
}

bool WaveformsUnderlay::mouseMove(const MouseEvent &e)
{
    auto &session = state->getActiveDocumentSession();
    auto &doc = session.document;
    auto &viewState = state->getActiveViewState();
    if (state->waveforms.empty() || doc.getChannelCount() <= 0 ||
        doc.getFrameCount() <= 0)
    {
        resetSampleValueUnderMouseCursor(state);
        handleChannelSelection(e.mouseYi, false);
        return false;
    }

    if (Waveform::shouldShowSamplePoints(viewState.samplesPerPixel,
                                         state->pixelScale))
    {
        const int channel = channelAt(e.mouseYi);

        if (channel >= 0 && channel < (int)state->waveforms.size())
        {
            auto *wf = state->waveforms[channel];
            const int64_t sampleIndex =
                static_cast<int64_t>(e.mouseXi * viewState.samplesPerPixel) +
                viewState.sampleOffset;

            wf->setSamplePosUnderCursor(sampleIndex);

            Waveform::clearHighlightIfNotChannel(state, channel);
        }
    }

    const int64_t sampleIndex = planWaveformsUnderlayValidSampleIndex(
        e.mouseXi, viewState.samplesPerPixel, viewState.sampleOffset,
        doc.getFrameCount());

    const uint8_t channel = channelAt(e.mouseYi);
    const float sampleValueUnderMouseCursor =
        doc.getSample(channel, sampleIndex);

    updateSampleValueUnderMouseCursor(state, sampleValueUnderMouseCursor,
                                      channel, sampleIndex);

    handleChannelSelection(e.mouseYi, false);

    if (!getWindow() || getWindow()->getCapturingComponent() != this ||
        !e.buttonState.left)
    {
        return false;
    }

    handleScroll(e.mouseXi);

    if (lastNumClicks == 1)
    {
        const int64_t snappedSamplePos =
            planSnappedMouseSamplePosition(state, e.mouseXf, true,
                                          draggedSelectionEdge);
        const bool selectionWasActive = session.selection.isActive();
        session.selection.setValue2(snappedSamplePos);
        if (selectionWasActive && !session.selection.isActive())
        {
            session.selection.setValue2(snappedSamplePos + 1.0);
        }
    }

    markAllWaveformsDirty();
    return true;
}

bool WaveformsUnderlay::mouseUp(const MouseEvent &e)
{
    auto &viewState = state->getActiveViewState();
    viewState.samplesToScroll = 0.0f;
    draggedSelectionEdge.reset();

    return true;
}

bool WaveformsUnderlay::mouseWheel(const MouseEvent &e)
{
    if (state->getActiveViewState().samplesPerPixel <= 0.0)
    {
        return false;
    }

    // SDL wheel.x > 0 means user scrolled right; move the viewport right.
    const double wheelX = static_cast<double>(e.wheelX);
    const double deltaPixels = wheelX * kPixelsPerWheelUnit;

    if (deltaPixels == 0.0)
    {
        return false;
    }

    lastHorizontalWheelEventTicks = SDL_GetTicks();
    horizontalWheelPendingPixels += deltaPixels;

    const bool moved = applyPendingHorizontalWheelScroll();
    return moved || std::abs(horizontalWheelPendingPixels) > 0.0;
}

void WaveformsUnderlay::timerCallback()
{
    const bool movedByWheel = applyPendingHorizontalWheelScroll();
    if (movedByWheel)
    {
        return;
    }

    auto &viewState = state->getActiveViewState();
    if (viewState.samplesToScroll == 0.0)
    {
        return;
    }

    const double samplesToScroll =
        viewState.samplesToScroll < 0.0
            ? std::min(-1.0, viewState.samplesToScroll)
            : std::max(1.0, viewState.samplesToScroll);
    const int64_t oldOffset = viewState.sampleOffset;
    const int64_t requestedOffset = static_cast<int64_t>(
        std::llround(static_cast<double>(viewState.sampleOffset) + samplesToScroll));
    const int64_t snappedOffset = Waveform::quantizeBlockScrollOffset(
        requestedOffset, getMaxSampleOffset(state), viewState.samplesPerPixel,
        state->pixelScale);
    updateSampleOffset(state, snappedOffset);
    if (oldOffset == viewState.sampleOffset)
    {
        return;
    }

    if (getWindow())
    {
        getWindow()->setComponentUnderMouse(nullptr);
    }
    const bool shouldUpdateSamplePoints = Waveform::shouldShowSamplePoints(
        viewState.samplesPerPixel, state->pixelScale);
    refreshWaveforms(state, shouldUpdateSamplePoints, true);
}

bool WaveformsUnderlay::applyPendingHorizontalWheelScroll()
{
    auto &viewState = state->getActiveViewState();
    if (viewState.samplesPerPixel <= 0.0 ||
        horizontalWheelPendingPixels == 0.0)
    {
        return false;
    }

    const auto stepPlan = planWaveformsUnderlayWheelStep(
        horizontalWheelPendingPixels, lastHorizontalWheelEventTicks,
        SDL_GetTicks(), kWheelSnapThresholdPixels, kWheelSmoothingFactor,
        kWheelStreamTimeoutMs);
    horizontalWheelPendingPixels = stepPlan.remainingPendingPixels;

    const auto deltaPlan = planWaveformsUnderlayWheelDelta(
        horizontalWheelRemainder, stepPlan.stepPixels,
        viewState.samplesPerPixel);
    horizontalWheelRemainder = deltaPlan.remainingSamples;
    const int64_t wholeSamples = deltaPlan.wholeSamples;
    if (wholeSamples == 0)
    {
        return false;
    }

    const int64_t oldOffset = viewState.sampleOffset;
    const int64_t requestedOffset = oldOffset + wholeSamples;
    const int64_t snappedOffset = Waveform::quantizeBlockScrollOffset(
        requestedOffset, getMaxSampleOffset(state), viewState.samplesPerPixel,
        state->pixelScale);
    updateSampleOffset(state, snappedOffset);
    horizontalWheelRemainder -= static_cast<double>(
        (viewState.sampleOffset - oldOffset) - wholeSamples);

    if (oldOffset == viewState.sampleOffset)
    {
        // We are clamped at an edge, so pending wheel deltas can be discarded.
        horizontalWheelPendingPixels = 0.0;
        horizontalWheelRemainder = 0.0;
        return false;
    }

    viewState.samplesToScroll = 0.0;
    if (getWindow())
    {
        getWindow()->setComponentUnderMouse(nullptr);
    }
    const bool shouldUpdateSamplePoints = Waveform::shouldShowSamplePoints(
        viewState.samplesPerPixel, state->pixelScale);
    refreshWaveforms(state, shouldUpdateSamplePoints, true);
    return true;
}

uint16_t WaveformsUnderlay::channelHeight() const
{
    const int64_t numChannels = state->waveforms.size();
    if (numChannels <= 0)
    {
        return static_cast<uint16_t>(std::max(0, getHeight()));
    }

    const int height = std::max(0, getHeight());
    return static_cast<uint16_t>(std::max<int>(1, height / numChannels));
}

uint8_t WaveformsUnderlay::channelAt(const uint16_t y) const
{
    if (state->waveforms.empty())
    {
        return 0;
    }
    const int ch = y / channelHeight();
    const int maxCh = static_cast<int>(state->waveforms.size()) - 1;
    return std::clamp(ch, 0, maxCh);
}

void WaveformsUnderlay::markAllWaveformsDirty() const
{
    for (auto *wf : state->waveforms)
    {
        wf->setDirty();
    }
}

void WaveformsUnderlay::handleScroll(const int32_t mouseX) const
{
    auto &viewState = state->getActiveViewState();
    const auto plan = planWaveformsUnderlayAutoScroll(
        mouseX, getWidth(), viewState.samplesPerPixel);
    viewState.samplesToScroll = plan.samplesToScroll;
}
