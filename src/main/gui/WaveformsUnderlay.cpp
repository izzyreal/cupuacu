#include "WaveformsUnderlay.hpp"

#include "Waveform.hpp"
#include "Window.hpp"
#include <algorithm>

#include <cassert>

using namespace cupuacu::gui;

int64_t getValidSampleIndexUnderMouseCursor(const int32_t mouseX,
                                            const double samplesPerPixel,
                                            const int64_t sampleOffset,
                                            const int64_t frameCount)
{
    assert(frameCount > 0);
    const int64_t sampleIndex =
        static_cast<int64_t>(mouseX * samplesPerPixel) + sampleOffset;
    return std::clamp(sampleIndex, int64_t{0}, frameCount - 1);
}

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
    auto &session = state->activeDocumentSession;
    auto &doc = session.document;
    auto &viewState = state->mainDocumentSessionWindow->getViewState();

    lastNumClicks = e.numClicks;

    handleChannelSelection(e.mouseYi, true);

    const auto samplesPerPixel = viewState.samplesPerPixel;
    const int channel = channelAt(e.mouseYi);
    const double selectionCenter =
        (session.selection.getStart() + session.selection.getEnd()) * 0.5;

    if (e.numClicks >= 2)
    {
        const double startSample = viewState.sampleOffset;
        double endSample =
            viewState.sampleOffset + getWidth() * samplesPerPixel;

        endSample = std::min((double)doc.getFrameCount(), endSample);

        if (samplesPerPixel < 1.0)
        {
            const double endFloor = std::floor(endSample);
            const double coverage = endSample - endFloor;

            if (coverage < 1.0)
            {
                endSample = endFloor;
            }
            else
            {
                endSample = endFloor + 1.0;
            }
        }

        session.selection.setValue1(startSample);
        session.selection.setValue2(endSample);

        Waveform::setAllWaveformsDirty(state);

        return true;
    }

    const auto *keyboard = SDL_GetKeyboardState(NULL);
    const bool shiftPressed =
        keyboard[SDL_SCANCODE_LSHIFT] || keyboard[SDL_SCANCODE_RSHIFT];

    const double samplePos =
        viewState.sampleOffset + e.mouseXf * samplesPerPixel;

    if (shiftPressed)
    {
        const double start = session.selection.getStart();
        const double end = session.selection.getEnd();

        if (samplePos < selectionCenter)
        {
            session.selection.setValue1(end);
            session.selection.setValue2(samplePos);
        }
        else
        {
            session.selection.setValue1(start);
            session.selection.setValue2(samplePos);
        }
    }
    else
    {
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
    auto &viewState = state->mainDocumentSessionWindow->getViewState();
    bool isLeftOnly = false;
    bool isRightOnly = false;

    for (size_t i = 0; i < state->waveforms.size(); ++i)
    {
        auto *wf = state->waveforms[i];
        const uint16_t yStart = i * channelHeight();
        const uint16_t yEnd = yStart + channelHeight();

        if (i == 0 && mouseY < yStart + channelHeight() / 4)
        {
            isLeftOnly = true;
        }
        else if (i == state->waveforms.size() - 1 &&
                 mouseY >= yEnd - channelHeight() / 4)
        {
            isRightOnly = true;
        }
    }

    assert(!(isLeftOnly && isRightOnly));

    if (isLeftOnly)
    {
        viewState.hoveringOverChannels = LEFT;
    }
    else if (isRightOnly)
    {
        viewState.hoveringOverChannels = RIGHT;
    }
    else
    {
        viewState.hoveringOverChannels = BOTH;
    }

    if ((getWindow() && getWindow()->getCapturingComponent() == this) ||
        isMouseDownEvent)
    {
        viewState.selectedChannels = viewState.hoveringOverChannels;
    }
}

bool WaveformsUnderlay::mouseMove(const MouseEvent &e)
{
    auto &session = state->activeDocumentSession;
    auto &doc = session.document;
    auto &viewState = state->mainDocumentSessionWindow->getViewState();

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

    const int64_t sampleIndex = getValidSampleIndexUnderMouseCursor(
        e.mouseXi, viewState.samplesPerPixel, viewState.sampleOffset,
        doc.getFrameCount());

    const uint8_t channel = channelAt(e.mouseYi);
    const float sampleValueUnderMouseCursor =
        doc.getSample(channel, sampleIndex);

    updateSampleValueUnderMouseCursor(state, sampleValueUnderMouseCursor);

    handleChannelSelection(e.mouseYi, false);

    if (!getWindow() || getWindow()->getCapturingComponent() != this ||
        !e.buttonState.left)
    {
        return false;
    }

    handleScroll(e.mouseXi, e.mouseYi);

    if (lastNumClicks == 1)
    {
        const double samplePos =
            viewState.sampleOffset + e.mouseXi * viewState.samplesPerPixel;
        const bool selectionWasActive = session.selection.isActive();

        session.selection.setValue2(samplePos);

        if (selectionWasActive && !session.selection.isActive())
        {
            session.selection.setValue2(samplePos + 1);
        }
    }

    markAllWaveformsDirty();
    return true;
}

bool WaveformsUnderlay::mouseUp(const MouseEvent &e)
{
    auto &viewState = state->mainDocumentSessionWindow->getViewState();
    viewState.samplesToScroll = 0.0f;

    return true;
}

void WaveformsUnderlay::timerCallback()
{
    auto &viewState = state->mainDocumentSessionWindow->getViewState();
    if (viewState.samplesToScroll == 0.0)
    {
        return;
    }

    const double samplesToScroll =
        viewState.samplesToScroll < 0.0
            ? std::min(-1.0, viewState.samplesToScroll)
            : std::max(1.0, viewState.samplesToScroll);
    const int64_t oldOffset = viewState.sampleOffset;

    updateSampleOffset(state, viewState.sampleOffset + samplesToScroll);

    if (oldOffset != viewState.sampleOffset)
    {
        if (getWindow())
        {
            getWindow()->setComponentUnderMouse(nullptr);
        }
        for (auto *wf : state->waveforms)
        {
            wf->updateSamplePoints();
        }
    }
}

uint16_t WaveformsUnderlay::channelHeight() const
{
    const int64_t numChannels = state->waveforms.size();
    return numChannels > 0 ? getHeight() / numChannels : getHeight();
}

uint8_t WaveformsUnderlay::channelAt(const uint16_t y) const
{
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

void WaveformsUnderlay::handleScroll(const int32_t mouseX,
                                     const int32_t mouseY) const
{
    auto &viewState = state->mainDocumentSessionWindow->getViewState();
    if (mouseX > getWidth() || mouseX < 0)
    {
        const auto samplesPerPixel = viewState.samplesPerPixel;
        const auto diff = mouseX < 0 ? mouseX : mouseX - getWidth();
        const auto samplesToScroll = diff * samplesPerPixel;
        viewState.samplesToScroll = samplesToScroll;
    }
    else
    {
        viewState.samplesToScroll = 0;
    }
}
