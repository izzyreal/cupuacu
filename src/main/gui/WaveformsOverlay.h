#pragma once
#include "Component.h"
#include "Waveform.h"
#include "../actions/Zoom.h"
#include <algorithm>

class WaveformsOverlay : public Component {
public:
    WaveformsOverlay(CupuacuState* stateToUse)
        : Component(stateToUse, "WaveformsOverlay") {}

    static float getSamplePosForMouseX(const int32_t mouseX,
                                         const double samplesPerPixel,
                                         const uint64_t sampleOffset,
                                         const size_t frameCount) 
    {
        const float xToUse = mouseX <= 0 ? 0.f : static_cast<float>(mouseX);
        return Waveform::xPositionToSampleIndex(xToUse, sampleOffset, samplesPerPixel, samplesPerPixel < 1.f, frameCount);
    }

    static size_t getValidSampleIndexUnderMouseCursor(const int32_t mouseX,
                                                 const double samplesPerPixel,
                                                 const double sampleOffset,
                                                 const size_t frameCount)
    {
        auto floatResult = getSamplePosForMouseX(mouseX, samplesPerPixel, sampleOffset, frameCount);
        
        if (floatResult >= frameCount)
        {
            floatResult = frameCount - 1;
        }

        return std::max(0.f, floatResult);
    }

    void mouseLeave() override
    {
        resetSampleValueUnderMouseCursor(state);
    }

    bool mouseLeftButtonDown(const uint8_t numClicks,
                             const int32_t mouseX,
                             const int32_t mouseY) override
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
                state->selectionChannelEnd   = static_cast<int>(state->waveforms.size()) - 1;
            }
            else
            {
                state->selectionAnchorChannel = channel;
                state->selectionChannelStart = channel;
                state->selectionChannelEnd = channel;
            }

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

        return true;
    }

    bool mouseMove(const int32_t mouseX,
                   const int32_t mouseY,
                   const float /*mouseRelY*/,
                   const bool leftButtonIsDown) override
    {
        if (Waveform::shouldShowSamplePoints(state->samplesPerPixel, state->pixelScale))
        {
            const int channel = channelAt(mouseY);
            
            if (channel >= 0 && channel < (int)state->waveforms.size())
            {
                auto* wf = state->waveforms[channel];
                const size_t sampleIndex = static_cast<size_t>(mouseX * state->samplesPerPixel) + state->sampleOffset;

                if (wf->samplePosUnderCursor != (int64_t)sampleIndex)
                {
                    wf->samplePosUnderCursor = (int64_t)sampleIndex;
                    wf->setDirtyRecursive();
                }

                for (size_t waveformChannel = 0; waveformChannel < state->waveforms.size(); ++waveformChannel)
                {
                    if (waveformChannel == channel)
                    {
                        continue;
                    }

                    state->waveforms[waveformChannel]->clearHighlight();
                }
            }
        }

        const int channel = channelAt(mouseY);
        const size_t sampleIndex = getValidSampleIndexUnderMouseCursor(mouseX, state->samplesPerPixel, state->sampleOffset, state->document.getFrameCount());

        const float sampleValueUnderMouseCursor = state->document.channels[channel][sampleIndex];

        state->sampleValueUnderMouseCursor = sampleValueUnderMouseCursor;

        if (state->capturingComponent != this || !leftButtonIsDown)
        {
            return false;
        }

        handleScroll(mouseX, mouseY);
        
        const double samplePos = state->sampleOffset + mouseX * state->samplesPerPixel;
        state->selection.setValue2(samplePos);

        state->selectionChannelStart = std::min(state->selectionAnchorChannel, channel);
        state->selectionChannelEnd   = std::max(state->selectionAnchorChannel, channel);

        markAllWaveformsDirty();
        return true;
    }

    bool mouseLeftButtonUp(const uint8_t numClicks,
                           const int32_t mouseX,
                           const int32_t mouseY) override
    {
        state->samplesToScroll = 0.0f;

        if (numClicks >= 2)
        {
            return true;
        }

        const double samplePos = state->sampleOffset + mouseX * state->samplesPerPixel;

        state->selection.setValue2(samplePos);

        int channel = channelAt(mouseY);
        state->selectionChannelStart = std::min(state->selectionChannelStart, channel);
        state->selectionChannelEnd   = std::max(state->selectionChannelEnd, channel);

        return true;
    }

    void timerCallback() override
    {
        if (state->samplesToScroll == 0.0)
        {
            return;
        }

        const double samplesToScroll = state->samplesToScroll < 0.0 ? std::min(-1.0, state->samplesToScroll) : std::max(1.0, state->samplesToScroll);
        const size_t oldOffset = state->sampleOffset;

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

private:
    int channelHeight()
    {
        int numChannels = static_cast<int>(state->waveforms.size());
        return numChannels > 0 ? getHeight() / numChannels : getHeight();
    }

    int channelAt(int y)
    {
        int ch = y / channelHeight();
        int maxCh = static_cast<int>(state->waveforms.size()) - 1;
        return std::clamp(ch, 0, maxCh);
    }

    void markAllWaveformsDirty()
    {
        for (auto* wf : state->waveforms)
        {
            wf->setDirtyRecursive();
        }
    }

    void handleScroll(int32_t mouseX, int32_t mouseY)
    {
        const auto samplesPerPixel = state->samplesPerPixel;
        const size_t oldSampleOffset = state->sampleOffset;

        if (mouseX > getWidth() || mouseX < 0)
        {
            auto diff = (mouseX < 0) ? mouseX : mouseX - getWidth();
            auto samplesToScroll = diff * samplesPerPixel;
            state->samplesToScroll = samplesToScroll;
        }
        else
        {
            state->samplesToScroll = 0;
        }

        const auto samplePos = getSamplePosForMouseX(mouseX, samplesPerPixel, state->sampleOffset, state->document.getFrameCount());
        state->selection.setValue2(samplePos);

        if (state->sampleOffset != oldSampleOffset)
        {
            for (auto* wf : state->waveforms)
            {
                wf->updateSamplePoints();
            }
        }
    }

};

