#include "MainView.h"

#include "../CupuacuState.h"
#include "Waveforms.h"
#include "Waveform.h"
#include "TriangleMarker.h"

MainView::MainView(CupuacuState *state) : Component(state, "MainView")
{
    waveforms = emplaceChildAndSetDirty<Waveforms>(state);
    rebuildWaveforms();

    cursorTop      = emplaceChildAndSetDirty<TriangleMarker>(state, TriangleMarkerType::CursorTop);
    cursorBottom   = emplaceChildAndSetDirty<TriangleMarker>(state, TriangleMarkerType::CursorBottom);
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

    updateTriangleMarkerBounds();
}

void MainView::updateTriangleMarkerBounds()
{
    const auto borderWidth = computeBorderWidth();
    const float triHeight = borderWidth * 0.75f;
    const float halfBase = triHeight;

    const auto sampleOffset = state->sampleOffset;
    const auto samplesPerPixel = state->samplesPerPixel;

    if (state->selection.isActive())
    {
        cursorTop->setBounds(0, 0, 0, 0);
        cursorBottom->setBounds(0, 0, 0, 0);

        const double tolerance = (samplesPerPixel < 1.0) ? (1.0 / samplesPerPixel) : 0.0;

        const float startXPosWithinWaveform = Waveform::getXPosForSampleIndex(state->selection.getStartInt(), sampleOffset, samplesPerPixel);
        const bool isStartMarkerVisible = startXPosWithinWaveform >= 0 && startXPosWithinWaveform <= waveforms->getWidth() + tolerance;

        if (isStartMarkerVisible)
        {
            selStartTop->setBounds(
                startXPosWithinWaveform + borderWidth,
                0,
                static_cast<int>(triHeight + 1.f),
                static_cast<int>(triHeight));

            selStartBot->setBounds(
                startXPosWithinWaveform + borderWidth,
                static_cast<int>(getHeight() - triHeight),
                static_cast<int>(triHeight + 1.f),
                static_cast<int>(triHeight));
        }
        else
        {
            selStartTop->setBounds(0, 0, 0, 0);
            selStartBot->setBounds(0, 0, 0, 0);
        }

        const int64_t endToUse = state->samplesPerPixel < 1 ? state->selection.getEndInt() + 1 : state->selection.getEndInt();
        const float endXPosWithinWaveform = Waveform::getXPosForSampleIndex(endToUse, sampleOffset, samplesPerPixel);

        const bool isEndMarkerVisible = endXPosWithinWaveform >= 0 && endXPosWithinWaveform <= waveforms->getWidth() + tolerance; 

        if (isEndMarkerVisible)
        {
            selEndTop->setBounds(
                endXPosWithinWaveform + borderWidth - triHeight,
                0,
                static_cast<int>(triHeight),
                static_cast<int>(triHeight));

            selEndBot->setBounds(
                endXPosWithinWaveform + borderWidth - triHeight,
                static_cast<int>(getHeight() - triHeight),
                static_cast<int>(triHeight),
                static_cast<int>(triHeight));
        }
        else
        {
            selEndTop->setBounds(0, 0, 0, 0);
            selEndBot->setBounds(0, 0, 0, 0);
        }
    }
    else
    {
        selStartTop->setBounds(0,0,0,0);
        selStartBot->setBounds(0,0,0,0);
        selEndTop->setBounds(0,0,0,0);
        selEndBot->setBounds(0,0,0,0);

        const float xPosWithinWaveform = Waveform::getXPosForSampleIndex(state->cursor, state->sampleOffset, state->samplesPerPixel);
        const bool isVisible = xPosWithinWaveform >= 0 && xPosWithinWaveform <= waveforms->getWidth();

        if (isVisible)
        {
            const float cursorX = xPosWithinWaveform + borderWidth;

            cursorTop->setBounds(
                static_cast<int>(cursorX - halfBase),
                0,
                static_cast<int>(halfBase * 2),
                static_cast<int>(triHeight));

            const int32_t cursorBottomYPos = getHeight() - triHeight;

            cursorBottom->setBounds(
                static_cast<int>(cursorX - halfBase),
                cursorBottomYPos,
                static_cast<int>(halfBase * 2),
                static_cast<int>(triHeight));
        }
        else
        {
            cursorTop->setBounds(0, 0, 0, 0);
            cursorBottom->setBounds(0, 0, 0, 0);
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

