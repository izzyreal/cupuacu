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

    if (state->selection.isActive())
    {
        drawSelectionTriangles(r);
    }
    else
    {
        drawCursorTriangles(r);
    }
}

void MainView::drawTriangle(SDL_Renderer *r,
                            const SDL_FPoint (&pts)[3],
                            const SDL_FColor &color)
{
    SDL_Vertex verts[3];
    for (int i = 0; i < 3; ++i) {
        verts[i].position  = pts[i];
        verts[i].color     = color;
        verts[i].tex_coord = {0.0f, 0.0f};
    }
    const int indices[3] = {0, 1, 2};
    SDL_RenderGeometry(r, nullptr, verts, 3, indices, 3);
}

void MainView::drawCursorTriangles(SDL_Renderer *r)
{
    const float borderWidth = static_cast<float>(computeBorderWidth());
    if (borderWidth <= 0.0f) return;

    const float triHeight = borderWidth * 0.75f;
    const float halfBase  = triHeight;
    const float winH      = static_cast<float>(getHeight());

    const float innerX = borderWidth;
    const float innerW = static_cast<float>(getWidth()) - borderWidth * 2.0f;

    const double cursorSample = static_cast<double>(state->cursor);
    const double firstVisible = static_cast<double>(state->sampleOffset);
    const double samplesPerPx = static_cast<double>(state->samplesPerPixel);

    const double pxWithinInner = (cursorSample - firstVisible) / samplesPerPx;
    const float cursorX = innerX + static_cast<float>(pxWithinInner);

    SDL_FColor triColor {188/255.f, 188/255.f, 0.0f, 1.0f};

    {
        SDL_FPoint pts[3] = {
            { cursorX, borderWidth },
            { cursorX - halfBase, borderWidth - triHeight },
            { cursorX + halfBase, borderWidth - triHeight }
        };
        drawTriangle(r, pts, triColor);
    }

    {
        const float tipY = winH - borderWidth;
        SDL_FPoint pts[3] = {
            { cursorX, tipY },
            { cursorX - halfBase, tipY + triHeight },
            { cursorX + halfBase, tipY + triHeight }
        };
        drawTriangle(r, pts, triColor);
    }
}

void MainView::drawSelectionTriangles(SDL_Renderer *r)
{
    const float borderWidth = static_cast<float>(computeBorderWidth());
    if (borderWidth <= 0.0f) return;

    const float triHeight = borderWidth * 0.75f;
    const float winH = static_cast<float>(getHeight());
    const float innerX = borderWidth;

    const double firstVisible = static_cast<double>(state->sampleOffset);
    const double samplesPerPx = static_cast<double>(state->samplesPerPixel);
    if (samplesPerPx <= 0.0) return;

    const auto sampleToX = [&](int sample) {
        const double pxWithinInner = (static_cast<double>(sample) - firstVisible) / samplesPerPx;
        return innerX + static_cast<float>(pxWithinInner);
    };

    const float startX = sampleToX(state->selection.getStartInt());
    const float endX   = sampleToX(state->selection.getEndInt());

    SDL_FColor triColor {188/255.f, 188/255.f, 0.0f, 1.0f};

    {
        SDL_FPoint pts[3] = {
            { startX, borderWidth - triHeight },
            { startX, borderWidth },
            { startX + triHeight, borderWidth - triHeight }
        };
        drawTriangle(r, pts, triColor);
    }
    {
        const float baseY = winH - borderWidth;
        SDL_FPoint pts[3] = {
            { startX, baseY + triHeight },
            { startX, baseY },
            { startX + triHeight, baseY + triHeight }
        };
        drawTriangle(r, pts, triColor);
    }
    {
        SDL_FPoint pts[3] = {
            { endX, borderWidth - triHeight },
            { endX, borderWidth },
            { endX - triHeight, borderWidth - triHeight }
        };
        drawTriangle(r, pts, triColor);
    }
    {
        const float baseY = winH - borderWidth;
        SDL_FPoint pts[3] = {
            { endX, baseY + triHeight },
            { endX, baseY },
            { endX - triHeight, baseY + triHeight }
        };
        drawTriangle(r, pts, triColor);
    }
}

void MainView::timerCallback()
{
    if (state->cursor != lastDrawnCursor ||
        state->selection.isActive() != lastSelectionIsActive ||
        state->samplesPerPixel != lastSamplesPerPixel ||
        state->sampleOffset != lastSampleOffset ||
        state->selection.getStartInt() != lastSelectionStart ||
        state->selection.getEndInt() != lastSelectionEnd)
    {
        lastDrawnCursor = state->cursor;
        lastSelectionIsActive = state->selection.isActive();
        lastSamplesPerPixel = state->samplesPerPixel;
        lastSampleOffset = state->sampleOffset;
        lastSelectionStart = state->selection.getStartInt();
        lastSelectionEnd = state->selection.getEndInt();
        setDirty();
    }
}

