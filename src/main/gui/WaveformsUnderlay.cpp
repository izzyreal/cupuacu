#include "WaveformsUnderlay.h"

#include "Waveform.h"
#include <algorithm>

#include <cassert>

int64_t getValidSampleIndexUnderMouseCursor(const int32_t mouseX,
                                             const double samplesPerPixel,
                                             const int64_t sampleOffset,
                                             const int64_t frameCount)
{
    assert(frameCount > 0);
    const int64_t sampleIndex = static_cast<int64_t>(mouseX * samplesPerPixel) + sampleOffset;
    return std::clamp(sampleIndex, int64_t{0}, frameCount - 1);
}

WaveformsUnderlay::WaveformsUnderlay(CupuacuState* stateToUse)
    : Component(stateToUse, "WaveformsUnderlay")
{
}

void WaveformsUnderlay::mouseLeave() 
{
    resetSampleValueUnderMouseCursor(state);
}

bool WaveformsUnderlay::mouseLeftButtonDown(const uint8_t numClicks,
                         const int32_t mouseX,
                         const int32_t mouseY) 
{
    lastNumClicks = numClicks;
    const auto samplesPerPixel = state->samplesPerPixel;
    const int channel = channelAt(mouseY);
    const double selectionCenter = (state->selection.getStart() + state->selection.getEnd()) * 0.5;
 
    if (numClicks >= 2)
    {
        double startSample = state->sampleOffset;
        double endSample   = state->sampleOffset + getWidth() * samplesPerPixel;

        endSample = std::min((double)state->document.getFrameCount(), endSample);

        if (samplesPerPixel < 1.0)
        {
            double endFloor = std::floor(endSample);
            double coverage = endSample - endFloor;

            if (coverage < 1.0)
            {
                endSample = endFloor;
            } else {
                endSample = endFloor + 1.0;
            }
        }

        state->selection.setValue1(startSample);
        state->selection.setValue2(endSample);

        handleChannelSelection(mouseY, true);
        
        Waveform::setAllWaveformsDirty(state);

        return true;
    }

    const auto *keyboard = SDL_GetKeyboardState(NULL);
    const bool shiftPressed = keyboard[SDL_SCANCODE_LSHIFT] || keyboard[SDL_SCANCODE_RSHIFT];

    const double samplePos = state->sampleOffset + mouseX * samplesPerPixel;

    if (shiftPressed)
    {
        const double start = state->selection.getStart();
        const double end = state->selection.getEnd();

        if (samplePos < selectionCenter)
        {
            state->selection.setValue1(end);
            state->selection.setValue2(samplePos);
        }
        else
        {
            state->selection.setValue1(start);
            state->selection.setValue2(samplePos);
        } 
    }
    else
    {
        state->selection.reset();
        state->selection.setValue1(samplePos);
        state->cursor = state->selection.getStartInt();
    }

    Waveform::setAllWaveformsDirty(state);

    return true;
}

void WaveformsUnderlay::handleChannelSelection(const int32_t mouseY, const bool isMouseDownEvent) const
{
    bool isLeftOnly = false;
    bool isRightOnly = false;

    for (size_t i = 0; i < state->waveforms.size(); ++i)
    {
        auto* wf = state->waveforms[i];
        uint16_t yStart = i * channelHeight();
        uint16_t yEnd = yStart + channelHeight();

        if (i == 0 && mouseY < yStart + channelHeight() / 4)
        {
            isLeftOnly = true;
        }
        else if (i == state->waveforms.size() - 1 && mouseY >= yEnd - channelHeight() / 4)
        {
            isRightOnly = true;
        }
    }

    assert(!(isLeftOnly && isRightOnly));

    if (isLeftOnly)
    {
        state->hoveringOverChannels = SelectedChannels::LEFT;
    }
    else if (isRightOnly)
    {
        state->hoveringOverChannels = SelectedChannels::RIGHT;
    }
    else
    {
        state->hoveringOverChannels = SelectedChannels::BOTH;
    }

    if (state->capturingComponent == this || isMouseDownEvent)
    {
        state->selectedChannels = state->hoveringOverChannels;
    }
}

bool WaveformsUnderlay::mouseMove(const int32_t mouseX,
               const int32_t mouseY,
               const float /*mouseRelY*/,
               const bool leftButtonIsDown) 
{
    if (Waveform::shouldShowSamplePoints(state->samplesPerPixel, state->pixelScale))
    {
        const int channel = channelAt(mouseY);
        
        if (channel >= 0 && channel < (int)state->waveforms.size())
        {
            auto* wf = state->waveforms[channel];
            const int64_t sampleIndex = static_cast<int64_t>(mouseX * state->samplesPerPixel) + state->sampleOffset;

            wf->setSamplePosUnderCursor(sampleIndex);

            Waveform::clearHighlightIfNotChannel(state, channel);
        }
    }

    const int64_t sampleIndex = getValidSampleIndexUnderMouseCursor(mouseX,
                                                                   state->samplesPerPixel,
                                                                   state->sampleOffset,
                                                                   state->document.getFrameCount());

    const uint8_t channel = channelAt(mouseY);
    const float sampleValueUnderMouseCursor = state->document.channels[channel][sampleIndex];

    updateSampleValueUnderMouseCursor(state, sampleValueUnderMouseCursor);

    handleChannelSelection(mouseY, false);

    if (state->capturingComponent != this || !leftButtonIsDown)
    {
        return false;
    }

    handleScroll(mouseX, mouseY);

    if (lastNumClicks == 1)
    {
        const double samplePos = state->sampleOffset + mouseX * state->samplesPerPixel;
        state->selection.setValue2(samplePos);
    }

    markAllWaveformsDirty();
    return true;
}

bool WaveformsUnderlay::mouseLeftButtonUp(const uint8_t numClicks,
                       const int32_t mouseX,
                       const int32_t mouseY) 
{
    state->samplesToScroll = 0.0f;

    if (state->selection.isActive())
    {
        state->cursor = state->selection.getStartInt();
    }

    return true;
}

void WaveformsUnderlay::timerCallback() 
{
    if (state->samplesToScroll == 0.0)
    {
        return;
    }

    const double samplesToScroll = state->samplesToScroll < 0.0 ? std::min(-1.0, state->samplesToScroll) : std::max(1.0, state->samplesToScroll);
    const int64_t oldOffset = state->sampleOffset;

    updateSampleOffset(state, state->sampleOffset + samplesToScroll);

    if (oldOffset != state->sampleOffset)
    {
        state->componentUnderMouse = nullptr;
        for (auto* wf : state->waveforms)
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
    int ch = y / channelHeight();
    int maxCh = static_cast<int>(state->waveforms.size()) - 1;
    return std::clamp(ch, 0, maxCh);
}

void WaveformsUnderlay::markAllWaveformsDirty()
{
    for (auto* wf : state->waveforms)
    {
        wf->setDirtyRecursive();
    }
}

void WaveformsUnderlay::handleScroll(const int32_t mouseX, const int32_t mouseY)
{
    if (mouseX > getWidth() || mouseX < 0)
    {
        const auto samplesPerPixel = state->samplesPerPixel;
        auto diff = (mouseX < 0) ? mouseX : mouseX - getWidth();
        auto samplesToScroll = diff * samplesPerPixel;
        state->samplesToScroll = samplesToScroll;
    }
    else
    {
        state->samplesToScroll = 0;
    }
}

