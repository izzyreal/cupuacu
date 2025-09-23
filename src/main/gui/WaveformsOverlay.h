#pragma once
#include "Component.h"
#include "Waveform.h"
#include <algorithm>

class WaveformsOverlay : public Component {
public:
    WaveformsOverlay(CupuacuState* stateToUse)
        : Component(stateToUse, "WaveformsOverlay") {}

    bool mouseLeftButtonDown(const uint8_t numClicks,
                             const int32_t mouseX,
                             const int32_t mouseY) override
    {
        const auto samplesPerPixel = state->samplesPerPixel;
        const double samplePos = state->sampleOffset + mouseX * samplesPerPixel;

        state->selection.setValue1(samplePos);
        state->selection.setValue2(samplePos);

        int channel = channelAt(mouseY);

        if (numClicks == 2)
        {
            double startSample = state->sampleOffset;
            double endSample   = state->sampleOffset + getWidth() * samplesPerPixel;

            state->selection.setValue1(startSample);
            state->selection.setValue2(endSample);

            state->selectionChannelStart = 0;
            state->selectionChannelEnd   = static_cast<int>(state->waveforms.size()) - 1;

            state->capturingComponent = nullptr;
            markAllWaveformsDirty();
            return true;
        }

        state->selectionChannelStart = channel;
        state->selectionChannelEnd   = channel;
        state->selectionAnchorChannel = channel;

        state->capturingComponent = this;
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
        
        const auto samplesPerPixel = state->samplesPerPixel;
        const double samplePos = state->sampleOffset + mouseX * samplesPerPixel;
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
        if (state->capturingComponent != this)
        {
            return false;
        }

        if (numClicks >= 2)
        {
            return true;
        }

        const auto samplesPerPixel = state->samplesPerPixel;
        const double samplePos = state->sampleOffset + mouseX * samplesPerPixel;
        state->selection.setValue2(samplePos);

        int channel = channelAt(mouseY);
        state->selectionChannelStart = std::min(state->selectionChannelStart, channel);
        state->selectionChannelEnd   = std::max(state->selectionChannelEnd, channel);

        state->capturingComponent = nullptr;
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

        auto waveformMouseX = mouseX;

        if (samplesPerPixel < 1.f)
        {
            waveformMouseX += 0.5f / samplesPerPixel;
        }

        const float x = waveformMouseX <= 0 ? 0.f : waveformMouseX;
        state->selection.setValue2(sampleOffset + (x * samplesPerPixel));

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

