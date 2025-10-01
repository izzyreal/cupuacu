#include "MainView.h"

#include "../CupuacuState.h"
#include "Waveforms.h"
#include "Waveform.h"
#include "TriangleMarker.h"

MainView::MainView(CupuacuState *state) : Component(state, "MainView")
{
    waveforms = emplaceChild<Waveforms>(state);
    rebuildWaveforms();

    cursorTop      = emplaceChild<TriangleMarker>(state, TriangleMarkerType::CursorTop);
    cursorBottom   = emplaceChild<TriangleMarker>(state, TriangleMarkerType::CursorBottom);
    selStartTop    = emplaceChild<TriangleMarker>(state, TriangleMarkerType::SelectionStartTop);
    selStartBot    = emplaceChild<TriangleMarker>(state, TriangleMarkerType::SelectionStartBottom);
    selEndTop      = emplaceChild<TriangleMarker>(state, TriangleMarkerType::SelectionEndTop);
    selEndBot      = emplaceChild<TriangleMarker>(state, TriangleMarkerType::SelectionEndBottom);
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

    if (state->selection.isActive())
    {
        cursorTop->setVisible(false);
        cursorBottom->setVisible(false);

        {
            const int32_t startX = Waveform::getXPosForSampleIndex(
                state->selection.getStartInt(),
                sampleOffset, samplesPerPixel);

            if (startX >= 0 && startX <= waveforms->getWidth())
            {
                selStartTop->setVisible(true);
                selStartBot->setVisible(true);

                selStartTop->setBounds(
                    startX + borderWidth,
                    0,
                    static_cast<int>(triHeight + 1.f),
                    static_cast<int>(triHeight));

                selStartBot->setBounds(
                    startX + borderWidth,
                    getHeight() - static_cast<int>(triHeight),
                    static_cast<int>(triHeight + 1.f),
                    static_cast<int>(triHeight));
            }
            else
            {
                selStartTop->setVisible(false);
                selStartBot->setVisible(false);
            }
        }

        {
            const int64_t endInclusive = state->selection.getEndInt();
            const int64_t endToUse = endInclusive + 1;

            const int32_t endX = Waveform::getXPosForSampleIndex(
                endToUse, sampleOffset, samplesPerPixel);

            if (endX >= 0 && endX <= waveforms->getWidth())
            {
                selEndTop->setVisible(true);
                selEndBot->setVisible(true);

                selEndTop->setBounds(
                    endX + borderWidth - static_cast<int>(triHeight),
                    0,
                    static_cast<int>(triHeight),
                    static_cast<int>(triHeight));

                selEndBot->setBounds(
                    endX + borderWidth - static_cast<int>(triHeight),
                    getHeight() - static_cast<int>(triHeight),
                    static_cast<int>(triHeight),
                    static_cast<int>(triHeight));
            }
            else
            {
                selEndTop->setVisible(false);
                selEndBot->setVisible(false);
            }
        }
    }
    else
    {
        selStartTop->setVisible(false);
        selStartBot->setVisible(false);
        selEndTop->setVisible(false);
        selEndBot->setVisible(false);

        const int32_t xPos = Waveform::getXPosForSampleIndex(
            state->cursor, sampleOffset, samplesPerPixel);

        if (xPos >= 0 && xPos <= waveforms->getWidth())
        {
            const int cursorX = xPos + borderWidth;

            cursorTop->setVisible(true);
            cursorBottom->setVisible(true);

            cursorTop->setBounds(
                cursorX - static_cast<int>(halfBase) + 1,
                0,
                static_cast<int>(halfBase * 2),
                static_cast<int>(triHeight));

            cursorBottom->setBounds(
                cursorX - static_cast<int>(halfBase) + 1,
                getHeight() - static_cast<int>(triHeight),
                static_cast<int>(halfBase * 2),
                static_cast<int>(triHeight));
        }
        else
        {
            cursorTop->setVisible(false);
            cursorBottom->setVisible(false);
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
        setDirty();
    }
}

