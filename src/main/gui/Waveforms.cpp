#include "Waveforms.h"

#include "Waveform.h"
#include "WaveformsUnderlay.h"

#include "../CupuacuState.h"

Waveforms::Waveforms(CupuacuState *state) :
    Component(state, "Waveforms")
{
    waveformsUnderlay = emplaceChild<WaveformsUnderlay>(state);
}

void Waveforms::rebuildWaveforms()
{
    for (auto &w : state->waveforms)
    {
        removeChild(w);
    }

    state->waveforms.clear();
    
    int numChannels = static_cast<int>(state->document.channels.size());

    if (numChannels > 0)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto waveform = std::make_unique<Waveform>(state, ch);
            auto *handle = addChild(waveform);
            state->waveforms.push_back(static_cast<Waveform*>(handle));
        }
    }
}

void Waveforms::resizeWaveforms()
{
    const int numChannels = static_cast<int>(state->waveforms.size());
    const float channelHeight = numChannels > 0 ? getHeight() / numChannels : 0;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        state->waveforms[ch]->setBounds(
            0,
            ch * channelHeight,
            getWidth(),
            channelHeight
        );
    }

    if (previousWidth != 0)
    {
        const auto oldSamplesPerPixelFactor = previousWidth * state->samplesPerPixel;
        const auto newSamplesPerPixel = oldSamplesPerPixelFactor / getWidth();
        state->samplesPerPixel = newSamplesPerPixel;
    }
}

void Waveforms::resized()
{
    waveformsUnderlay->setBounds(0, 0, getWidth(), getHeight());
    resizeWaveforms();
    previousWidth = getWidth();
}

