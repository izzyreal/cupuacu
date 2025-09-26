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
    const auto samplesPerPixel = state->samplesPerPixel;
    const int channel = channelAt(mouseY);

    if (numClicks >= 2)
    {
        double startSample = state->sampleOffset;
        double endSample   = state->sampleOffset + getWidth() * samplesPerPixel;
        endSample -= 0.5;
        endSample = std::min((double)state->document.getFrameCount(), endSample);

        state->selection.setValue1(startSample);
        state->selection.setValue2(endSample);

        if (numClicks >= 3)
        {
            state->selectionAnchorChannel = 0;
            state->selectionChannelStart = 0;
            state->selectionChannelEnd = static_cast<int>(state->waveforms.size()) - 1;
        }
        else
        {
            state->selectionAnchorChannel = channel;
            state->selectionChannelStart = channel;
            state->selectionChannelEnd = channel;
        }

        Waveform::setAllWaveformsDirty(state);

        return true;
    }

    const auto *keyboard = SDL_GetKeyboardState(NULL);
    const bool shiftPressed = keyboard[SDL_SCANCODE_LSHIFT] || keyboard[SDL_SCANCODE_RSHIFT];

    const double samplePos = state->sampleOffset + mouseX * samplesPerPixel;

    if (!shiftPressed)
    {
        state->selection.reset();
    }

    if (shiftPressed && !state->selection.isActive())
    {
        state->selection.setValue1(state->playbackPosition.load());
    }

    if (!shiftPressed || !state->selection.isActive())
    {
        state->selection.setValue1(samplePos);
        state->selectionAnchorChannel = channel;
        state->selectionChannelStart = channel;
    }

    state->selection.setValue2(samplePos);
    state->selectionChannelEnd   = channel;
    state->playbackPosition.store(std::round(samplePos));

    Waveform::setAllWaveformsDirty(state);

    return true;
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

    if (state->capturingComponent != this || !leftButtonIsDown)
    {
        return false;
    }

    handleScroll(mouseX, mouseY);
    
    const double samplePos = state->sampleOffset + mouseX * state->samplesPerPixel;
    state->selection.setValue2(samplePos);

    assert(state->selectionAnchorChannel.has_value());

    state->selectionChannelStart.emplace(std::min(*state->selectionAnchorChannel, channel));
    state->selectionChannelEnd.emplace(std::max(*state->selectionAnchorChannel, channel));

    markAllWaveformsDirty();
    return true;
}

bool WaveformsUnderlay::mouseLeftButtonUp(const uint8_t numClicks,
                       const int32_t mouseX,
                       const int32_t mouseY) 
{
    state->samplesToScroll = 0.0f;

    if (numClicks >= 2)
    {
        return true;
    }

    const double samplePos = state->sampleOffset + mouseX * state->samplesPerPixel;

    state->selection.setValue2(samplePos);

    const uint8_t channel = channelAt(mouseY);
    assert(state->selectionChannelStart.has_value());
    assert(state->selectionChannelEnd.has_value());
    state->selectionChannelStart.emplace(std::min(*state->selectionChannelStart, channel));
    state->selectionChannelEnd.emplace(std::max(*state->selectionChannelEnd, channel));

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

