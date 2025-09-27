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

    const int64_t sampleOffset = state->sampleOffset;
    const double samplesPerPx  = state->samplesPerPixel;
    if (samplesPerPx <= 0.0) return;

    const float cursorX = (state->cursor - sampleOffset) / samplesPerPx;

    if (cursorX < 0.0f || cursorX > innerW)
        return;

    const float screenCursorX = innerX + cursorX;

    SDL_FColor triColor {188/255.f, 188/255.f, 0.0f, 1.0f};

    {
        SDL_FPoint pts[3] = {
            { screenCursorX, borderWidth },
            { screenCursorX - halfBase, borderWidth - triHeight },
            { screenCursorX + halfBase, borderWidth - triHeight }
        };
        drawTriangle(r, pts, triColor);
    }

    {
        const float tipY = winH - borderWidth;
        SDL_FPoint pts[3] = {
            { screenCursorX, tipY },
            { screenCursorX - halfBase, tipY + triHeight },
            { screenCursorX + halfBase, tipY + triHeight }
        };
        drawTriangle(r, pts, triColor);
    }
}

void MainView::drawSelectionTriangles(SDL_Renderer *r)
{
    const float borderWidth = static_cast<float>(computeBorderWidth());
    if (borderWidth <= 0.0f) return;

    const float triHeight = borderWidth * 0.75f;
    const float winH      = static_cast<float>(getHeight());
    const float innerX    = borderWidth;
    const float innerW    = static_cast<float>(getWidth()) - borderWidth * 2.0f;

    const double firstVisible = static_cast<double>(state->sampleOffset);
    const double samplesPerPx = static_cast<double>(state->samplesPerPixel);
    if (samplesPerPx <= 0.0) return;

    const auto sampleToScreenX = [&](int sample, float& outX) -> bool {
        const double pxWithinInner = (static_cast<double>(sample) - firstVisible) / samplesPerPx;
        const double tolerance = (samplesPerPx < 1.0) ? (1.0 / samplesPerPx) : 0.0;
        if (pxWithinInner < 0.0 || pxWithinInner > innerW + tolerance)
            return false;
        outX = innerX + static_cast<float>(pxWithinInner);
        return true;
    };

    float startX, endX;
    SDL_FColor triColor {188/255.f, 188/255.f, 0.0f, 1.0f};

    if (sampleToScreenX(state->selection.getStartInt(), startX)) {
        SDL_FPoint topPts[3] = {
            { startX, borderWidth - triHeight },
            { startX, borderWidth },
            { startX + triHeight, borderWidth - triHeight }
        };
        drawTriangle(r, topPts, triColor);

        const float baseY = winH - borderWidth;
        SDL_FPoint bottomPts[3] = {
            { startX, baseY + triHeight },
            { startX, baseY },
            { startX + triHeight, baseY + triHeight }
        };
        drawTriangle(r, bottomPts, triColor);
    }

    int64_t selectionEnd = state->selection.getEndInt();
    if (state->samplesPerPixel < 1)
    {
        selectionEnd++;
    }

    if (sampleToScreenX(selectionEnd, endX)) {
        SDL_FPoint topPts[3] = {
            { endX, borderWidth - triHeight },
            { endX, borderWidth },
            { endX - triHeight, borderWidth - triHeight }
        };
        drawTriangle(r, topPts, triColor);

        const float baseY = winH - borderWidth;
        SDL_FPoint bottomPts[3] = {
            { endX, baseY + triHeight },
            { endX, baseY },
            { endX - triHeight, baseY + triHeight }
        };
        drawTriangle(r, bottomPts, triColor);
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

