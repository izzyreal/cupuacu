#include "MainView.h"
#include "../CupuacuState.h"
#include "Waveforms.h"
#include "Waveform.h"
#include "TriangleMarker.h"
#include "OpaqueRect.h"
#include "Timeline.h"

MainView::MainView(CupuacuState *state) : Component(state, "MainView")
{
    for (int i = 0; i < 4; ++i)
    {
        borders[i] = emplaceChild<OpaqueRect>(state, Colors::border);
    }

    waveforms = emplaceChild<Waveforms>(state);
    timeline  = emplaceChild<Timeline>(state);
    rebuildWaveforms();

    cursorTop      = borders[0]->emplaceChild<TriangleMarker>(state, TriangleMarkerType::CursorTop);
    selStartTop    = borders[0]->emplaceChild<TriangleMarker>(state, TriangleMarkerType::SelectionStartTop);
    selEndTop      = borders[0]->emplaceChild<TriangleMarker>(state, TriangleMarkerType::SelectionEndTop);

    cursorBottom   = borders[1]->emplaceChild<TriangleMarker>(state, TriangleMarkerType::CursorBottom);
    selStartBot    = borders[1]->emplaceChild<TriangleMarker>(state, TriangleMarkerType::SelectionStartBottom);
    selEndBot      = borders[1]->emplaceChild<TriangleMarker>(state, TriangleMarkerType::SelectionEndBottom);
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
    const int width  = getWidth();
    const int height = getHeight();

    int timelineHeight = static_cast<int>(60 / state->pixelScale);

    waveforms->setBounds(borderWidth,
                         borderWidth,
                         width - 2 * borderWidth,
                         height - 2 * borderWidth - timelineHeight);

    borders[0]->setBounds(0, 0, width, borderWidth);
    borders[1]->setBounds(0, height - borderWidth, width, borderWidth);
    borders[2]->setBounds(0, borderWidth, borderWidth, height - 2 * borderWidth);
    borders[3]->setBounds(width - borderWidth, borderWidth, borderWidth, height - 2 * borderWidth);

    timeline->setBounds(borderWidth,
                        height - borderWidth - timelineHeight,
                        width - 2 * borderWidth,
                        timelineHeight);

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

        const int32_t startX = Waveform::getXPosForSampleIndex(state->selection.getStartInt(),
                                                              sampleOffset, samplesPerPixel);
        if (startX >= 0 && startX <= waveforms->getWidth())
        {
            selStartTop->setVisible(true);
            selStartBot->setVisible(true);
            selStartTop->setBounds(startX + borderWidth, 0,
                                  static_cast<int>(triHeight + 1.f), static_cast<int>(triHeight));
            selStartBot->setBounds(startX + borderWidth, 0,
                                  static_cast<int>(triHeight + 1.f), static_cast<int>(triHeight));
        }
        else
        {
            selStartTop->setVisible(false);
            selStartBot->setVisible(false);
        }

        const int64_t endInclusive = state->selection.getEndInt();
        const int64_t endToUse = endInclusive + 1;
        const int32_t endX = Waveform::getXPosForSampleIndex(endToUse, sampleOffset, samplesPerPixel);

        if (endX >= 0 && endX <= waveforms->getWidth())
        {
            selEndTop->setVisible(true);
            selEndBot->setVisible(true);
            selEndTop->setBounds(endX + borderWidth - static_cast<int>(triHeight), 0,
                                static_cast<int>(triHeight), static_cast<int>(triHeight));
            selEndBot->setBounds(endX + borderWidth - static_cast<int>(triHeight), 0,
                                static_cast<int>(triHeight), static_cast<int>(triHeight));
        }
        else
        {
            selEndTop->setVisible(false);
            selEndBot->setVisible(false);
        }
    }
    else
    {
        selStartTop->setVisible(false);
        selStartBot->setVisible(false);
        selEndTop->setVisible(false);
        selEndBot->setVisible(false);

        const int32_t xPos = Waveform::getXPosForSampleIndex(state->cursor, sampleOffset, samplesPerPixel);
        if (xPos >= 0 && xPos <= waveforms->getWidth())
        {
            const int cursorX = xPos + borderWidth;
            cursorTop->setVisible(true);
            cursorBottom->setVisible(true);
            cursorTop->setBounds(cursorX - static_cast<int>(halfBase) + 1, 0,
                                static_cast<int>(halfBase * 2), static_cast<int>(triHeight));
            cursorBottom->setBounds(cursorX - static_cast<int>(halfBase) + 1, 0,
                                   static_cast<int>(halfBase * 2), static_cast<int>(triHeight));
        }
        else
        {
            cursorTop->setVisible(false);
            cursorBottom->setVisible(false);
        }
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

        updateTriangleMarkerBounds();
        setDirty();
    }
}
