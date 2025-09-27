#include "MainView.h"

#include "../CupuacuState.h"

#include "Waveforms.h"

MainView::MainView(CupuacuState *state) : Component(state, "MainView")
{
    waveforms = emplaceChildAndSetDirty<Waveforms>(state);
    rebuildWaveforms();
}

uint8_t MainView::computeBorderWidth() const
{
    return baseBorderWidth / state->pixelScale;
}

void MainView::rebuildWaveforms()
{
    waveforms->rebuildWaveforms();
    waveforms->resizeWaveforms();
}

void MainView::resized()
{
    const auto borderWidth = computeBorderWidth();
    waveforms->setBounds(borderWidth, borderWidth, getWidth() - (borderWidth * 2), getHeight() - (borderWidth * 2));
}

void MainView::onDraw(SDL_Renderer *r)
{
    SDL_SetRenderDrawColor(r, 128, 128, 0, 255);
    SDL_FRect rectToFill {0.f, 0.f, (float)getWidth(), (float)getHeight()};
    SDL_RenderFillRect(r, &rectToFill);
}

