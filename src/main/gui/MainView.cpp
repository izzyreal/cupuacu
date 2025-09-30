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
    const auto borderWidth    = computeBorderWidth();
    const float triHeight     = borderWidth * 0.75f;
    const float halfBase      = triHeight;

    const auto sampleOffset   = state->sampleOffset;
    const auto samplesPerPixel = state->samplesPerPixel;

    const double tolerance = (samplesPerPixel < 1.0) ? (1.0 / samplesPerPixel) : 0.0;

    if (state->selection.isActive())
    {
        cursorTop->setBounds(0, 0, 0, 0);
        cursorBottom->setBounds(0, 0, 0, 0);

        {
            const float startX = Waveform::getXPosForSampleIndex(
                state->selection.getStartInt(),
                sampleOffset, samplesPerPixel);

            const bool visible =
                startX >= -tolerance &&
                startX <= waveforms->getWidth() + tolerance;

            if (visible)
            {
                const float clampedX = std::clamp(startX, 0.f, (float)waveforms->getWidth());

                selStartTop->setBounds(
                    clampedX + borderWidth,
                    0,
                    (int)(triHeight + 1.f),
                    (int)triHeight);

                selStartBot->setBounds(
                    clampedX + borderWidth,
                    getHeight() - (int)triHeight,
                    (int)(triHeight + 1.f),
                    (int)triHeight);
            }
            else
            {
                selStartTop->setBounds(0, 0, 0, 0);
                selStartBot->setBounds(0, 0, 0, 0);
            }
        }

        {
            const int64_t endInclusive = state->selection.getEndInt();
            const int64_t endToUse = endInclusive + 1;

            const float endX = Waveform::getXPosForSampleIndex(
                endToUse, sampleOffset, samplesPerPixel);

            const bool visible =
                endX >= -tolerance &&
                endX <= waveforms->getWidth() + tolerance;

            if (visible)
            {
                const float clampedX = std::clamp(endX, 0.f, (float)waveforms->getWidth());

                selEndTop->setBounds(
                    clampedX + borderWidth - triHeight,
                    0,
                    (int)triHeight,
                    (int)triHeight);

                selEndBot->setBounds(
                    clampedX + borderWidth - triHeight,
                    getHeight() - (int)triHeight,
                    (int)triHeight,
                    (int)triHeight);
            }
            else
            {
                selEndTop->setBounds(0, 0, 0, 0);
                selEndBot->setBounds(0, 0, 0, 0);
            }
        }
    }
    else
    {
        selStartTop->setBounds(0, 0, 0, 0);
        selStartBot->setBounds(0, 0, 0, 0);
        selEndTop->setBounds(0, 0, 0, 0);
        selEndBot->setBounds(0, 0, 0, 0);

        const float xPos = Waveform::getXPosForSampleIndex(
            state->cursor, sampleOffset, samplesPerPixel);

        const bool visible =
            xPos >= -tolerance &&
            xPos <= waveforms->getWidth() + tolerance;

        if (visible)
        {
            const float clampedX = std::clamp(xPos, 0.f, (float)waveforms->getWidth());
            const float cursorX  = clampedX + borderWidth;

            cursorTop->setBounds(
                (int)(cursorX - halfBase),
                0,
                (int)(halfBase * 2),
                (int)triHeight);

            cursorBottom->setBounds(
                (int)(cursorX - halfBase),
                getHeight() - (int)triHeight,
                (int)(halfBase * 2),
                (int)triHeight);
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

