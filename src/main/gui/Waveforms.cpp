#include "Waveforms.hpp"

#include "Waveform.hpp"
#include "MainViewLayoutPlanning.hpp"
#include "WaveformsUnderlay.hpp"

#include "../State.hpp"

using namespace cupuacu::gui;

Waveforms::Waveforms(State *state) : Component(state, "Waveforms")
{
    waveformsUnderlay = emplaceChild<WaveformsUnderlay>(state);
}

void Waveforms::onDraw(SDL_Renderer *renderer)
{
    // Always paint the waveform area so stale popup/menu pixels are cleared
    // even when there are zero channel waveform children.
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, nullptr);
}

void Waveforms::rebuildWaveforms()
{
    const auto oldWaveforms = state->waveforms;
    state->waveforms.clear();

    for (auto *w : oldWaveforms)
    {
        removeChild(w);
    }

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
        const auto tiles =
            planWaveformChannelTiles(getWidth(), getHeight(), numChannels);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto &tile = tiles[static_cast<size_t>(ch)];
            state->waveforms[ch]->setBounds(tile.x, tile.y, tile.w, tile.h);
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
