#include "Waveforms.hpp"

#include "Waveform.hpp"
#include "MainViewLayoutPlanning.hpp"
#include "WaveformsUnderlay.hpp"

#include "../State.hpp"
#include "../actions/ZoomPlanning.hpp"

using namespace cupuacu::gui;

namespace
{
    void rescaleAllTabViewStatesForWaveformWidthChange(cupuacu::State *state,
                                                       const int previousWidth,
                                                       const int newWidth)
    {
        if (!state || previousWidth <= 0 || newWidth <= 0 ||
            previousWidth == newWidth)
        {
            return;
        }

        for (auto &tab : state->tabs)
        {
            auto &viewState = tab.viewState;
            if (viewState.samplesPerPixel <= 0.0)
            {
                continue;
            }

            const double visibleSampleCount =
                static_cast<double>(previousWidth) * viewState.samplesPerPixel;
            viewState.samplesPerPixel =
                visibleSampleCount / static_cast<double>(newWidth);
            viewState.sampleOffset = std::clamp<int64_t>(
                viewState.sampleOffset, 0,
                cupuacu::actions::planMaxSampleOffset(
                    tab.session.document.getFrameCount(), newWidth,
                    viewState.samplesPerPixel));
        }
    }
} // namespace

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
        state->getActiveDocumentSession().document.getChannelCount();

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

    rescaleAllTabViewStatesForWaveformWidthChange(state, previousWidth,
                                                  getWidth());
}

void Waveforms::resized()
{
    waveformsUnderlay->setBounds(0, 0, getWidth(), getHeight());
    resizeWaveforms();
    previousWidth = getWidth();
}
