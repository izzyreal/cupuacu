#pragma once
#include "Component.h"
#include "Waveform.h"
#include <algorithm>

class WaveformsOverlay : public Component {
public:
    WaveformsOverlay(CupuacuState* stateToUse)
        : Component(stateToUse, "WaveformsOverlay") {}

    static float getSamplePosForMouseX(const int32_t mouseX,
                                         const double samplesPerPixel,
                                         const double sampleOffset) 
    {
        const float xToUse = mouseX <= 0 ? 0.f : static_cast<float>(mouseX);
        return Waveform::xPositionToSampleIndex(xToUse, sampleOffset, samplesPerPixel, samplesPerPixel < 1.f);
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

            markAllWaveformsDirty();
            return true;
        }

        const auto *keyboard = SDL_GetKeyboardState(NULL);
        const bool shiftPressed = keyboard[SDL_SCANCODE_LSHIFT] || keyboard[SDL_SCANCODE_RSHIFT];

        const double samplePos = state->sampleOffset + mouseX * samplesPerPixel;

        if (!shiftPressed || !state->selection.isActive())
        {
            state->selection.setValue1(samplePos);
            state->selectionAnchorChannel = channel;
            state->selectionChannelStart = channel;
        }

        state->selection.setValue2(samplePos);
        state->selectionChannelEnd   = channel;
        state->playbackPosition.store(samplePos);

        markAllWaveformsDirty();
        return true;
    }

    bool mouseMove(const int32_t mouseX,
                   const int32_t mouseY,
                   const float /*mouseRelY*/,
                   const bool leftButtonIsDown) override
    {
        if (state->capturingComponent != this || !leftButtonIsDown)
        {
            return false;
        }

        handleScroll(mouseX, mouseY);
        
        const auto samplePos = getSamplePosForMouseX(mouseX, state->samplesPerPixel, state->sampleOffset); 
        state->selection.setValue2(samplePos);

        int channel = channelAt(mouseY);
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

        const auto samplePos = getSamplePosForMouseX(mouseX, state->samplesPerPixel, state->sampleOffset); 
        state->selection.setValue2(samplePos);

        int channel = channelAt(mouseY);
        state->selectionChannelStart = std::min(state->selectionChannelStart, channel);
        state->selectionChannelEnd   = std::max(state->selectionChannelEnd, channel);

        markAllWaveformsDirty();
        return true;
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
            if (wf)
            {
                wf->setDirtyRecursive();
            }
        }
    }

    void handleScroll(int32_t mouseX, int32_t mouseY)
    {
        const auto samplesPerPixel = state->samplesPerPixel;
        const auto oldSampleOffset = state->sampleOffset;
        auto sampleOffset = state->sampleOffset;

        if (mouseX > getWidth() || mouseX < 0)
        {
            auto diff = (mouseX < 0) ? mouseX : mouseX - getWidth();
            auto samplesToScroll = diff * samplesPerPixel;

            if (samplesToScroll < 0)
            {
                double absScroll = -samplesToScroll;
                state->sampleOffset = (state->sampleOffset > absScroll)
                    ? state->sampleOffset - absScroll
                    : 0;
            }
            else
            {
                state->sampleOffset += samplesToScroll;
            }

            sampleOffset = state->sampleOffset;
            state->samplesToScroll = samplesToScroll;
        }
        else
        {
            state->samplesToScroll = 0;
        }

        const auto samplePos = getSamplePosForMouseX(mouseX, samplesPerPixel, sampleOffset);
        state->selection.setValue2(samplePos);

        markAllWaveformsDirty();

        if (state->sampleOffset != oldSampleOffset)
        {
            for (auto* wf : state->waveforms)
            {
                if (wf) wf->updateSamplePoints();
            }
        }
    }

};

