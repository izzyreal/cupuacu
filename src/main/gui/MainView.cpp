#include "MainView.h"

#include "../CupuacuState.h"

#include "Waveforms.h"

MainView::MainView(CupuacuState *state) : Component(state, "MainView")
{
    waveforms = emplaceChildAndSetDirty<Waveforms>(state);
    rebuildWaveforms();
}

void MainView::rebuildWaveforms()
{
    waveforms->rebuildWaveforms();
    waveforms->resizeWaveforms();
}

void MainView::resized()
{
    waveforms->setBounds(5, 5, getWidth() - 10, getHeight() - 10);
}

void MainView::onDraw(SDL_Renderer *r)
{
    SDL_SetRenderDrawColor(r, 128, 128, 0, 255);
    SDL_FRect rectToFill {0.f, 0.f, (float)getWidth(), (float)getHeight()};
    SDL_RenderFillRect(r, &rectToFill);
}

