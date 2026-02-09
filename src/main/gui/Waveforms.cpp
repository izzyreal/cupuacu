#include "Waveforms.hpp"

#include "Waveform.hpp"
#include "WaveformsUnderlay.hpp"

#include "../State.hpp"

using namespace cupuacu::gui;

Waveforms::Waveforms(State *state) : Component(state, "Waveforms")
{
    waveformsUnderlay = emplaceChild<WaveformsUnderlay>(state);
}

void Waveforms::rebuildWaveforms()
{
    for (const auto &w : state->waveforms)
    {
        removeChild(w);
    }

    state->waveforms.clear();

    const int numChannels =
        state->activeDocumentSession.document.getChannelCount();

    if (numChannels > 0)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto waveform = std::make_unique<Waveform>(state, ch);
            auto *handle = addChild(waveform);
            state->waveforms.push_back(static_cast<Waveform *>(handle));
        }
    }
}

void Waveforms::resizeWaveforms() const
{
    auto &viewState = state->mainDocumentSessionWindow->getViewState();
    const int numChannels = static_cast<int>(state->waveforms.size());
    if (numChannels > 0)
    {
        const int totalHeight = getHeight();
        const int baseHeight = totalHeight / numChannels;
        const int remainder = totalHeight % numChannels;

        int yPos = 0;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const int channelHeight = baseHeight + (ch < remainder ? 1 : 0);
            state->waveforms[ch]->setBounds(0, yPos, getWidth(), channelHeight);
            yPos += channelHeight;
        }
    }

    if (previousWidth != 0)
    {
        const auto oldSamplesPerPixelFactor =
            previousWidth * viewState.samplesPerPixel;
        const auto newSamplesPerPixel = oldSamplesPerPixelFactor / getWidth();
        viewState.samplesPerPixel = newSamplesPerPixel;
    }
}

void Waveforms::resized()
{
    waveformsUnderlay->setBounds(0, 0, getWidth(), getHeight());
    resizeWaveforms();
    previousWidth = getWidth();
}
