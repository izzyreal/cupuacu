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
                                         const double sampleOffset,
                                         const size_t frameCount) 
    {
        const float xToUse = mouseX <= 0 ? 0.f : static_cast<float>(mouseX);
        return Waveform::xPositionToSampleIndex(xToUse, sampleOffset, samplesPerPixel, samplesPerPixel < 1.f, frameCount);
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

            markAllWaveformsDirty();
            return true;
        }

        const auto *keyboard = SDL_GetKeyboardState(NULL);
        const bool shiftPressed = keyboard[SDL_SCANCODE_LSHIFT] || keyboard[SDL_SCANCODE_RSHIFT];

        const double samplePos = state->sampleOffset + mouseX * samplesPerPixel;

        if (!shiftPressed)
        {
            state->selection.reset();
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
        const double samplePos = state->sampleOffset + mouseX * state->samplesPerPixel;
        
        //const auto samplePos = getSamplePosForMouseX(mouseX, state->samplesPerPixel, state->sampleOffset, state->document.getFrameCount()); 
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

        //const auto samplePos = getSamplePosForMouseX(mouseX, state->samplesPerPixel, state->sampleOffset, state->document.getFrameCount()); 
        const double samplePos = state->sampleOffset + mouseX * state->samplesPerPixel;

        state->selection.setValue2(samplePos);

        int channel = channelAt(mouseY);
        state->selectionChannelStart = std::min(state->selectionChannelStart, channel);
        state->selectionChannelEnd   = std::max(state->selectionChannelEnd, channel);

        markAllWaveformsDirty();
        return true;
    }

    void timerCallback() override
    {
        if (state->samplesToScroll != 0.0)
        {
            const double scroll = state->samplesToScroll;
            const uint64_t oldOffset = state->sampleOffset;
            const double maxOffset =
                std::max(0.0, state->document.getFrameCount() - getWidth() * state->samplesPerPixel);

            if (scroll < 0)
            {
                const double absScroll = -scroll;
                state->sampleOffset = (state->sampleOffset > absScroll)
                                    ? state->sampleOffset - absScroll
                                    : 0;
            }
            else
            {
                state->sampleOffset = std::min(state->sampleOffset + scroll, maxOffset);
            }

            snapSampleOffset(state);

            if (oldOffset != state->sampleOffset)
            {
                state->componentUnderMouse = nullptr;
                for (auto* wf : state->waveforms)
                {
                    if (wf) wf->updateSamplePoints();
                }
                markAllWaveformsDirty();
            }
        }

        const int mouseX = state->mouseX;
        const int mouseY = state->mouseY;

        if (Waveform::shouldShowSamplePoints(state->samplesPerPixel, state->hardwarePixelsPerAppPixel))
        {
            for (auto* wf : state->waveforms)
            {
                if (wf && wf->samplePosUnderCursor != -1)
                {
                    wf->samplePosUnderCursor = -1;
                    wf->setDirtyRecursive();
                }
            }

            const int channel = channelAt(mouseY);
            
            if (channel >= 0 && channel < (int)state->waveforms.size())
            {
                auto* wf = state->waveforms[channel];
                if (wf)
                {
                    const auto samplePos =
                        getSamplePosForMouseX(mouseX, state->samplesPerPixel, state->sampleOffset, state->document.getFrameCount());
                    if (wf->samplePosUnderCursor != (int64_t)samplePos)
                    {
                        wf->samplePosUnderCursor = (int64_t)samplePos;
                        wf->setDirtyRecursive();
                    }
                }
            }
        }
        else
        {
            for (auto* wf : state->waveforms)
            {
                if (wf && wf->samplePosUnderCursor != -1)
                {
                    wf->samplePosUnderCursor = -1;
                    wf->setDirtyRecursive();
                }
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

        const auto samplePos = getSamplePosForMouseX(mouseX, samplesPerPixel, sampleOffset, state->document.getFrameCount());
        state->selection.setValue2(samplePos);

        markAllWaveformsDirty();

        if (state->sampleOffset != oldSampleOffset)
        {
            const double maxOffset = std::max(0.0, state->document.getFrameCount() - getWidth() * state->samplesPerPixel);

            state->sampleOffset = std::min(maxOffset, state->sampleOffset);
            
            for (auto* wf : state->waveforms)
            {
                if (wf) wf->updateSamplePoints();
            }
        }
    }

};

