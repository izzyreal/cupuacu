#include "MainView.h"

#include "../CupuacuState.h"

#include "WaveformsUnderlay.h"
#include "Waveform.h"

MainView::MainView(CupuacuState *state) : Component(state, "MainView")
{
    waveformsUnderlay = emplaceChildAndSetDirty<WaveformsUnderlay>(state);
    rebuildWaveforms();
}

void MainView::rebuildWaveforms()
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
            auto *handle = addChildAndSetDirty(waveform);
            state->waveforms.push_back(static_cast<Waveform*>(handle));
        }
    }

    resizeWaveforms();
}

void MainView::resized()
{
    waveformsUnderlay->setBounds(0, 0, getWidth(), getHeight());
    resizeWaveforms();
}

void MainView::resizeWaveforms()
{
    const auto oldSamplesPerPixelFactor = Waveform::getWaveformWidth(state) * state->samplesPerPixel;
    const auto newSamplesPerPixel = oldSamplesPerPixelFactor / state->waveforms[0]->getWidth();
    state->samplesPerPixel = newSamplesPerPixel;

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
}

