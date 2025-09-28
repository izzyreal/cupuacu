#include "MainView.h"

#include "../CupuacuState.h"
#include "Waveforms.h"
#include "TriangleMarker.h"   // NEW

#include <cmath>

MainView::MainView(CupuacuState *state) : Component(state, "MainView")
{
    waveforms = emplaceChildAndSetDirty<Waveforms>(state);
    rebuildWaveforms();

    // NEW: add markers
    cursorTop = emplaceChildAndSetDirty<TriangleMarker>(state, TriangleMarkerType::CursorTop);
    cursorBottom = emplaceChildAndSetDirty<TriangleMarker>(state, TriangleMarkerType::CursorBottom);
    selStartTop    = emplaceChildAndSetDirty<TriangleMarker>(state, TriangleMarkerType::SelectionStartTop);
    selStartBot    = emplaceChildAndSetDirty<TriangleMarker>(state, TriangleMarkerType::SelectionStartBottom);
    selEndTop      = emplaceChildAndSetDirty<TriangleMarker>(state, TriangleMarkerType::SelectionEndTop);
    selEndBot      = emplaceChildAndSetDirty<TriangleMarker>(state, TriangleMarkerType::SelectionEndBottom);
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
    waveforms->setBounds(borderWidth, borderWidth,
                         getWidth() - (borderWidth * 2),
                         getHeight() - (borderWidth * 2));

}

void MainView::updateTriangleMarkerBounds()
{
    const auto borderWidth = computeBorderWidth();
    const float bw = static_cast<float>(borderWidth);
    const float triHeight = bw * 0.75f;
    const float halfBase = triHeight;
    const float winH = static_cast<float>(getHeight());

    const float innerX = bw;
    const float innerW = static_cast<float>(getWidth()) - bw * 2.0f;

    const double samplesPerPx = state->samplesPerPixel;
    if (samplesPerPx <= 0.0) return;
    const int64_t sampleOffset = state->sampleOffset;

    if (state->selection.isActive())
    {
        cursorTop->setBounds(0, 0, 0, 0);
        cursorBottom->setBounds(0, 0, 0, 0);

        auto sampleToScreenX = [&](int sample, float& outX) -> bool {
            const double pxWithinInner = (static_cast<double>(sample) - sampleOffset) / samplesPerPx;
            const double tolerance = (samplesPerPx < 1.0) ? (1.0 / samplesPerPx) : 0.0;
            if (pxWithinInner < 0.0 || pxWithinInner > innerW + tolerance)
                return false;
            outX = innerX + static_cast<float>(pxWithinInner);
            return true;
        };

        float startX;
        if (sampleToScreenX(state->selection.getStartInt(), startX)) {
            startX = std::round(startX);

            selStartTop->setBounds(
                static_cast<int>(startX),
                0,
                static_cast<int>(triHeight + 1.f),
                static_cast<int>(triHeight));

            selStartBot->setBounds(
                static_cast<int>(startX),
                static_cast<int>(winH - triHeight),
                static_cast<int>(triHeight + 1.f),
                static_cast<int>(triHeight));
        }

        int64_t selectionEnd = state->selection.getEndInt();
        if (samplesPerPx < 1.0) {
            selectionEnd++;
        }

        float endX;
        if (sampleToScreenX(selectionEnd, endX)) {
            endX = std::round(endX);

            selEndTop->setBounds(
                static_cast<int>(endX - triHeight),
                0,
                static_cast<int>(triHeight),
                static_cast<int>(triHeight));

            selEndBot->setBounds(
                static_cast<int>(endX - triHeight),
                static_cast<int>(winH - triHeight),
                static_cast<int>(triHeight),
                static_cast<int>(triHeight));
        }
    }
    else
    {
        const float cursorX = (state->cursor - sampleOffset) / samplesPerPx;
        if (cursorX >= 0.0f && cursorX <= innerW) {
            const float screenX = std::round(innerX + cursorX) + 1.f;

            cursorTop->setBounds(
                static_cast<int>(screenX - halfBase),
                0,
                static_cast<int>(halfBase * 2),
                static_cast<int>(triHeight));

            cursorBottom->setBounds(
                static_cast<int>(screenX - halfBase),
                static_cast<int>(winH - triHeight),
                static_cast<int>(halfBase * 2),
                static_cast<int>(triHeight));
        }
    }
}

void MainView::onDraw(SDL_Renderer *r)
{
    SDL_SetRenderDrawColor(r, 28, 28, 28, 255);
    SDL_FRect rectToFill {0.f, 0.f, (float)getWidth(), (float)getHeight()};
    SDL_RenderFillRect(r, &rectToFill);
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
        updateTriangleMarkerBounds();
        setDirtyRecursive();
    }
}

