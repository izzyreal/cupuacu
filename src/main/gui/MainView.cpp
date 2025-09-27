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
    SDL_SetRenderDrawColor(r, 28, 28, 28, 255);
    SDL_FRect rectToFill {0.f, 0.f, (float)getWidth(), (float)getHeight()};
    SDL_RenderFillRect(r, &rectToFill);

    if (!state->selection.isActive())
    {
        drawCursorTriangles(r);
    }
}
void MainView::drawCursorTriangles(SDL_Renderer *r)
{
    const float borderWidth = static_cast<float>(computeBorderWidth());
    if (borderWidth <= 0.0f) return;

    const float triHeight = borderWidth * 0.75f;
    const float halfBase  = triHeight;
    const float winH      = static_cast<float>(getHeight());

    // waveform drawable region
    const float innerX = borderWidth;
    const float innerW = static_cast<float>(getWidth()) - borderWidth * 2.0f;

    // cursor in samples → pixels
    const double cursorSample = static_cast<double>(state->cursor);
    const double firstVisible = static_cast<double>(state->sampleOffset);
    const double samplesPerPx = static_cast<double>(state->samplesPerPixel);

    const double pxWithinInner = (cursorSample - firstVisible) / samplesPerPx;
    const float cursorX = innerX + static_cast<float>(pxWithinInner);

    SDL_FColor triColor {188/255.f, 188/255.f, 0.0f, 1.0f};
    SDL_FPoint zeroTex {0.0f, 0.0f};

    // --- top triangle ---
    {
        SDL_Vertex verts[3];
        verts[0].position = { cursorX, borderWidth };              // tip
        verts[1].position = { cursorX - halfBase, borderWidth - triHeight };
        verts[2].position = { cursorX + halfBase, borderWidth - triHeight };

        for (int i = 0; i < 3; ++i) {
            verts[i].color = triColor;
            verts[i].tex_coord = zeroTex;
        }

        const int indices[3] = {0, 1, 2};
        SDL_RenderGeometry(r, nullptr, verts, 3, indices, 3);
    }

    // --- bottom triangle ---
    {
        const float tipY = winH - borderWidth;
        SDL_Vertex verts[3];
        verts[0].position = { cursorX, tipY };                      // tip
        verts[1].position = { cursorX - halfBase, tipY + triHeight };
        verts[2].position = { cursorX + halfBase, tipY + triHeight };

        for (int i = 0; i < 3; ++i) {
            verts[i].color = triColor;
            verts[i].tex_coord = zeroTex;
        }

        const int indices[3] = {0, 1, 2};
        SDL_RenderGeometry(r, nullptr, verts, 3, indices, 3);
    }
}

void MainView::timerCallback()
{
    if (state->cursor != lastDrawnCursor ||
        state->selection.isActive() != lastSelectionIsActive)
    {
        lastDrawnCursor = state->cursor;
        lastSelectionIsActive = state->selection.isActive();
        setDirty();
    }
}

